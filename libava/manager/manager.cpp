#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <nvml.h>
#include <grpc++/grpc++.h>

#include <algorithm>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "manager.h"
#include "manager_service.grpc.fb.h"
#include "manager_service_generated.h"
#include "common/cmd_channel_impl.h"
#include "common/cmd_handler.h"
#include "common/socket.h"

int listen_fd;

__sighandler_t original_sigint_handler = SIG_DFL;

void sigint_handler(int signo) {
  if (listen_fd > 0)
    close(listen_fd);
  signal(signo, original_sigint_handler);
  raise(signo);
}

class ManagerConfig {
public:
  static int const kDefaultManagerPort;
  static int const kDefaultWorkerPoolSize;

  ManagerConfig(int mp = kDefaultManagerPort, int wps = kDefaultWorkerPoolSize)
      : manager_port_(mp), worker_pool_size_(wps) {}

  void Print() {
    std::cerr << "* Manager port: " << manager_port_ << std::endl
              << "* API server pool size: " << worker_pool_size_ << std::endl;
  }

  int manager_port_;
  int worker_pool_size_;
  std::vector<std::unique_ptr<DaemonInfo>> daemons_;
};

int const ManagerConfig::kDefaultManagerPort    = 3334;
int const ManagerConfig::kDefaultWorkerPoolSize = 3;

std::shared_ptr<ManagerConfig> config;

std::shared_ptr<ManagerConfig> parse_arguments(int argc, char* argv[]) {
  int c;
  opterr               = 0;
  int manager_port     = ManagerConfig::kDefaultManagerPort;
  int worker_pool_size = ManagerConfig::kDefaultWorkerPoolSize;

  while ((c = getopt(argc, argv, "m:n:")) != -1) {
    switch (c) {
    case 'm':
      manager_port = (uint32_t)atoi(optarg);
      break;
    case 'n':
      worker_pool_size = (uint32_t)atoi(optarg);
      break;
    default:
      fprintf(stderr,
              "Usage: %s "
              "[-m manager_port {%d}] "
              "[-n worker_pool_size {%d}]\n",
              argv[0], ManagerConfig::kDefaultManagerPort,
              ManagerConfig::kDefaultWorkerPoolSize);
      exit(EXIT_FAILURE);
    }
  }

  return std::make_shared<ManagerConfig>(manager_port, worker_pool_size);
}

class DaemonServiceClient {
public:
  DaemonServiceClient(std::shared_ptr<grpc::Channel> channel)
      : stub_(DaemonService::NewStub(channel)) {}

  std::vector<std::string> SpawnWorker(std::vector<int>& count,
                                       std::vector<std::string>& uuid,
                                       std::string daemon_ip = "0.0.0.0") {
    /* Build message. */
    flatbuffers::grpc::MessageBuilder mb;
    std::vector<flatbuffers::Offset<flatbuffers::String>> _uuid;
    for (auto const& uu : uuid)
      _uuid.push_back(mb.CreateString(uu));
    auto cnt_offset     = mb.CreateVector(&count[0], count.size());
    auto uu_offset      = mb.CreateVector(&_uuid[0], _uuid.size());
    auto request_offset = CreateWorkerSpawnRequest(mb, cnt_offset, uu_offset);
    mb.Finish(request_offset);
    auto request_msg = mb.ReleaseMessage<WorkerSpawnRequest>();

    /* Send request. */
    flatbuffers::grpc::Message<WorkerSpawnReply> response_msg;
    grpc::ClientContext context;
    auto status = stub_->SpawnWorker(&context, request_msg, &response_msg);

    /* Parse response. */
    std::vector<std::string> worker_address;
    if (status.ok()) {
      const WorkerSpawnReply* response = response_msg.GetRoot();
      auto wa                          = response->worker_address();
      for (auto const& addr_offset : *wa) {
        std::string _wa = daemon_ip + ":" + addr_offset->str();
        std::cerr << "Register API server at " << _wa << std::endl;
        worker_address.push_back(_wa);
      }
    } else {
      std::cerr << status.error_code() << ": " << status.error_message()
                << std::endl;
    }
    return worker_address;
  }

private:
  std::unique_ptr<DaemonService::Stub> stub_;
};

class ManagerServiceImpl final : public ManagerService::Service {
  virtual grpc::Status RegisterDaemon(
      grpc::ServerContext* context,
      const flatbuffers::grpc::Message<DaemonRegisterRequest>* request_msg,
      flatbuffers::grpc::Message<DaemonRegisterReply>* response_msg) override {
    const DaemonRegisterRequest* request = request_msg->GetRoot();
    std::string peer_address =
        context->peer().substr(context->peer().find(':') + 1);
    const std::string daemon_ip =
        peer_address.substr(0, peer_address.find(':'));
    const std::string daemon_address =
        daemon_ip + ":" + request->daemon_address()->str();
    std::cerr << "Register spawn daemon at " << daemon_address << std::endl;

    /* Register GPU information in a global table */
    auto daemon_info = std::make_unique<DaemonInfo>();
    daemon_info->ip_ = daemon_ip;
    for (auto const& uu_offset : *(request->uuid()))
      daemon_info->gpu_info_.push_back({uu_offset->str(), 0});
    int idx = 0;
    for (auto fm : *(request->free_memory())) {
      daemon_info->gpu_info_[idx].free_memory_ = fm;
      ++idx;
    }
    daemon_info->PrintGpuInfo();

    /* Request daemon to spawn an API server pool.
     * Currently each API server can see only one GPU, and every GPU has
     * `config->worker_pool_sizea_` API servers running on it. */
    auto channel =
        grpc::CreateChannel(daemon_address, grpc::InsecureChannelCredentials());
    daemon_info->client_ = std::make_unique<DaemonServiceClient>(channel);
    std::vector<int> count;
    std::vector<std::string> uuid;
    for (auto const& gi : daemon_info->gpu_info_) {
      count.push_back(config->worker_pool_size_);
      uuid.push_back(gi.uuid_);
    }
    std::vector<std::string> worker_address =
        daemon_info->client_->SpawnWorker(count, uuid, daemon_ip);

    /* Register API servers in a global table */
    int k = 0;
    for (unsigned i = 0; i < count.size(); ++i)
      for (int j = 0; j < count[i]; ++j) {
        daemon_info->workers_.Enqueue(worker_address[k], uuid[i]);
        ++k;
      }

    config->daemons_.push_back(std::move(daemon_info));
    return grpc::Status::OK;
  }
};

void runManagerService(std::shared_ptr<ManagerConfig> config) {
  std::string server_address("0.0.0.0:" +
                             std::to_string(config->manager_port_));
  ManagerServiceImpl service;

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cerr << "Manager Service listening on " << server_address << std::endl;
  server->Wait();
}

void replyToGuestlib(int client_fd, std::string worker_address) {
  struct command_base response;
  uintptr_t* worker_port;
  int assigned_worker_port =
      std::stoi(worker_address.substr(worker_address.find(':') + 1));

  response.api_id = INTERNAL_API;
  worker_port     = (uintptr_t*)response.reserved_area;
  *worker_port    = assigned_worker_port;
  send_socket(client_fd, &response, sizeof(struct command_base));
}

void handleGuestlib(int client_fd, std::shared_ptr<ManagerConfig> config) {
  struct command_base msg;

  /* get guestlib info */
  recv_socket(client_fd, &msg, sizeof(struct command_base));
  switch (msg.command_type) {
  case NW_NEW_APPLICATION:
    /* API server assignment policy.
     *
     * Currently it is assumed that every API server is running on only one GPU.
     * The policy simply assigns an available API server to the application;
     * if no idle API server exists, the manager requests a daemon to spawn
     * a new API server.
     *
     * The manager has the information of the free GPU memory on each GPU node,
     * while this information is not currently used as we need annotations of
     * every application's GPU usage or retrieve GPU usage dynamically.
     *
     * `config->daemons_` should be protected with locks in case of hot-plugged
     * spawn daemons. For now it is assumed that all daemons have been spawned
     * before any guestlib connects in.
     */
    for (auto const& daemon : config->daemons_) {
      auto assigned_worker = daemon->workers_.Dequeue();
      if (!assigned_worker.first.empty()) {
        /* Find an idle API server, respond guestlib and spawn a new idle
         * worker. */
        replyToGuestlib(client_fd, assigned_worker.first);
        close(client_fd);

        std::vector<int> count        = {1};
        std::vector<std::string> uuid = {assigned_worker.second};
        std::vector<std::string> worker_address =
            daemon->client_->SpawnWorker(count, uuid, daemon->ip_);
        daemon->workers_.Enqueue(worker_address[0], assigned_worker.second);
        return;
      }
    }

    /* No idle API server was found, spawn a new API server.
     * Currently, we constantly spawn the new API server on the first GPU of the
     * first GPU node (which is assumed to exist).
     */
    {
      std::vector<int> count        = {1};
      std::vector<std::string> uuid = {config->daemons_[0]->gpu_info_[0].uuid_};
      std::vector<std::string> worker_address =
          config->daemons_[0]->client_->SpawnWorker(count, uuid,
                                                    config->daemons_[0]->ip_);
      replyToGuestlib(client_fd,
                      worker_address.empty() ? "0.0.0.0:0" : worker_address[0]);
      close(client_fd);
    }
    break;

  default:
    std::cerr << "Received unrecognized message " << msg.command_type
              << " from guestlib" << std::endl;
    close(client_fd);
  }
}

/**
 * This routine functions as the traditional AvA manager, listening on a
 * hard-coded port WORKER_MANAGER_PORT (3333). The guestlib connects to this
 * port to request for the API server. We are getting rid of those hard-coded
 * configurations, but this waits for some AvA upstream changes. The final goal
 * is to merge this into the ManagerService gRPC routine.
 */
void startTraditionalManager() {
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  int opt     = 1;
  int client_fd;

  /* Setup signal handler. */
  if ((original_sigint_handler = signal(SIGINT, sigint_handler)) == SIG_ERR)
    printf("failed to catch SIGINT\n");

  /* Initialize TCP socket. */
  if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket");
  }
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
  }
  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port        = htons(WORKER_MANAGER_PORT);

  if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    perror("bind failed");
  }
  if (listen(listen_fd, 10) < 0) {
    perror("listen");
  }

  /* Polling new applications. */
  do {
    client_fd =
        accept(listen_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
    std::async(std::launch::async | std::launch::deferred, handleGuestlib,
               client_fd, config);
  } while (1);
}

int main(int argc, char* argv[]) {
  config = parse_arguments(argc, argv);
  config->Print();

  std::thread server_thread(runManagerService, config);
  startTraditionalManager();
  server_thread.join();

  return 0;
}