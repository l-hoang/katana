/** Local Computation graphs -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the
 * License.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2017, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 * @author Loc Hoang <l_hoang@utexas.edu>
 */

#ifndef GALOIS_GRAPH__LC_CSR_GRAPH_H
#define GALOIS_GRAPH__LC_CSR_GRAPH_H

#include "galois/Galois.h"
#include "galois/LargeArray.h"
#include "galois/graphs/FileGraph.h"
#include "galois/graphs/Details.h"
#include "galois/graphs/GraphHelpers.h"

#include <type_traits>

namespace galois {
namespace graphs {
/**
 * Local computation graph (i.e., graph structure does not change). The data 
 * representation is the traditional compressed-sparse-row (CSR) format.
 *
 * The position of template parameters may change between Galois releases; the
 * most robust way to specify them is through the with_XXX nested templates.
 *
 * An example of use:
 *
 * \snippet test/graph.cpp Using a graph
 *
 * And in C++11:
 *
 * \snippet test/graph.cpp Using a graph cxx11
 *
 * @tparam NodeTy data on nodes
 * @tparam EdgeTy data on out edges
 */
template<typename NodeTy, typename EdgeTy,
         bool HasNoLockable=false,
         bool UseNumaAlloc=false,
         bool HasOutOfLineLockable=false,
         typename FileEdgeTy=EdgeTy>
class LC_CSR_Graph :
     private boost::noncopyable,
     private internal::LocalIteratorFeature<UseNumaAlloc>,
     private internal::OutOfLineLockableFeature<HasOutOfLineLockable &&
                                                !HasNoLockable> {
  template<typename Graph> friend class LC_InOut_Graph;
 public:
  template<bool _has_id>
  struct with_id { typedef LC_CSR_Graph type; };

  template<typename _node_data>
  struct with_node_data { 
    typedef LC_CSR_Graph<_node_data, EdgeTy, HasNoLockable, UseNumaAlloc,HasOutOfLineLockable,FileEdgeTy> type;
  };

  template<typename _edge_data>
  struct with_edge_data { 
    typedef LC_CSR_Graph<NodeTy,_edge_data,HasNoLockable,UseNumaAlloc,HasOutOfLineLockable,FileEdgeTy> type; 
  };

  template<typename _file_edge_data>
  struct with_file_edge_data { typedef LC_CSR_Graph<NodeTy,EdgeTy,HasNoLockable,UseNumaAlloc,HasOutOfLineLockable,_file_edge_data> type; };

  //! If true, do not use abstract locks in graph
  template<bool _has_no_lockable>
  struct with_no_lockable { typedef LC_CSR_Graph<NodeTy,EdgeTy,_has_no_lockable,UseNumaAlloc,HasOutOfLineLockable,FileEdgeTy> type; };
  template<bool _has_no_lockable>
  using _with_no_lockable = LC_CSR_Graph<NodeTy,EdgeTy,_has_no_lockable,UseNumaAlloc,HasOutOfLineLockable,FileEdgeTy>;

  //! If true, use NUMA-aware graph allocation
  template<bool _use_numa_alloc>
  struct with_numa_alloc { typedef LC_CSR_Graph<NodeTy,EdgeTy,HasNoLockable,_use_numa_alloc,HasOutOfLineLockable,FileEdgeTy> type; };
  template<bool _use_numa_alloc>
  using _with_numa_alloc = LC_CSR_Graph<NodeTy,EdgeTy,HasNoLockable,_use_numa_alloc,HasOutOfLineLockable,FileEdgeTy>;

  //! If true, store abstract locks separate from nodes
  template<bool _has_out_of_line_lockable>
  struct with_out_of_line_lockable { typedef LC_CSR_Graph<NodeTy,EdgeTy,HasNoLockable,UseNumaAlloc,_has_out_of_line_lockable,FileEdgeTy> type; };

  typedef read_default_graph_tag read_tag;

 protected:
  typedef LargeArray<EdgeTy> EdgeData;
  typedef LargeArray<uint32_t> EdgeDst;
  typedef internal::NodeInfoBaseTypes<NodeTy,!HasNoLockable && !HasOutOfLineLockable> NodeInfoTypes;
  typedef internal::NodeInfoBase<NodeTy,!HasNoLockable && !HasOutOfLineLockable> NodeInfo;
  typedef LargeArray<uint64_t> EdgeIndData;
  typedef LargeArray<NodeInfo> NodeData;

 public:
  typedef uint32_t GraphNode;
  typedef EdgeTy edge_data_type;
  typedef FileEdgeTy file_edge_data_type;
  typedef NodeTy node_data_type;
  typedef typename EdgeData::reference edge_data_reference;
  typedef typename NodeInfoTypes::reference node_data_reference;
  typedef boost::counting_iterator<typename EdgeIndData::value_type> edge_iterator;
  typedef boost::counting_iterator<typename EdgeDst::value_type> iterator;
  typedef iterator const_iterator;
  typedef iterator local_iterator;
  typedef iterator const_local_iterator;

 protected:
  NodeData nodeData;
  EdgeIndData edgeIndData;
  EdgeDst edgeDst;
  EdgeData edgeData;

  uint64_t numNodes;
  uint64_t numEdges;

  // used to track division of nodes among threads; for numa aware allocation
  // + division of work among threads during do_all
  std::vector<uint32_t> threadRanges;
  // used to track division of edges among threads; mainly for numa aware 
  // allocation of edges
  std::vector<uint64_t> threadRangesEdge;

  typedef internal::EdgeSortIterator<GraphNode,typename EdgeIndData::value_type,EdgeDst,EdgeData> edge_sort_iterator;
 
  edge_iterator raw_begin(GraphNode N) const {
    return edge_iterator((N == 0) ? 0 : edgeIndData[N-1]);
  }

  edge_iterator raw_end(GraphNode N) const {
    return edge_iterator(edgeIndData[N]);
  }

  edge_sort_iterator edge_sort_begin(GraphNode N) {
    return edge_sort_iterator(*raw_begin(N), &edgeDst, &edgeData);
  }

  edge_sort_iterator edge_sort_end(GraphNode N) {
    return edge_sort_iterator(*raw_end(N), &edgeDst, &edgeData);
  }

  template<bool _A1 = HasNoLockable, bool _A2 = HasOutOfLineLockable>
  void acquireNode(GraphNode N, MethodFlag mflag, typename std::enable_if<!_A1 && !_A2>::type* = 0) {
    galois::runtime::acquire(&nodeData[N], mflag);
  }

  template<bool _A1 = HasOutOfLineLockable, bool _A2 = HasNoLockable>
  void acquireNode(GraphNode N, MethodFlag mflag, typename std::enable_if<_A1 && !_A2>::type* = 0) {
    this->outOfLineAcquire(getId(N), mflag);
  }

  template<bool _A1 = HasOutOfLineLockable, bool _A2 = HasNoLockable>
  void acquireNode(GraphNode N, MethodFlag mflag, typename std::enable_if<_A2>::type* = 0) { }

  template<bool _A1 = EdgeData::has_value, bool _A2 = LargeArray<FileEdgeTy>::has_value>
  void constructEdgeValue(FileGraph& graph, typename FileGraph::edge_iterator nn, 
      typename std::enable_if<!_A1 || _A2>::type* = 0) {
    typedef LargeArray<FileEdgeTy> FED;
    if (EdgeData::has_value)
      edgeData.set(*nn, graph.getEdgeData<typename FED::value_type>(nn));
  }

  template<bool _A1 = EdgeData::has_value, bool _A2 = LargeArray<FileEdgeTy>::has_value>
  void constructEdgeValue(FileGraph& graph, typename FileGraph::edge_iterator nn,
      typename std::enable_if<_A1 && !_A2>::type* = 0) {
    edgeData.set(*nn, {});
  }

  size_t getId(GraphNode N) {
    return N;
  }

  GraphNode getNode(size_t n) {
    return n;
  }

 public:
  LC_CSR_Graph(LC_CSR_Graph&& rhs) = default;
  LC_CSR_Graph() = default;
  LC_CSR_Graph& operator=(LC_CSR_Graph&&) = default;

  /**
   * Accesses the "prefix sum" of this graph; takes advantage of the fact
   * that edge_end(n) is basically prefix_sum[n] (if a prefix sum existed +
   * if prefix_sum[0] = number of edges in node 0).
   *
   * ONLY USE IF GRAPH HAS BEEN LOADED
   *
   * @param n Index into edge prefix sum
   * @returns The value that would be located at index n in an edge prefix sum 
   * array
   */
  uint64_t operator[](uint64_t n) { 
    return *(edge_end(n));
  }

  /**
   * Clear thread ranges.
   */
  void clearRanges() {
    threadRanges.clear();
    threadRangesEdge.clear();
  }

  template<typename EdgeNumFnTy, typename EdgeDstFnTy, typename EdgeDataFnTy>
  LC_CSR_Graph(uint32_t _numNodes, uint64_t _numEdges,
               EdgeNumFnTy edgeNum, EdgeDstFnTy _edgeDst, EdgeDataFnTy _edgeData)
   : numNodes(_numNodes), numEdges(_numEdges) {
    //std::cerr << "\n**" << numNodes << " " << numEdges << "\n\n";
    if (UseNumaAlloc) {
      nodeData.allocateLocal(numNodes);
      edgeIndData.allocateLocal(numNodes);
      edgeDst.allocateLocal(numEdges);
      edgeData.allocateLocal(numEdges);
      this->outOfLineAllocateLocal(numNodes, false);
    } else {
      nodeData.allocateInterleaved(numNodes);
      edgeIndData.allocateInterleaved(numNodes);
      edgeDst.allocateInterleaved(numEdges);
      edgeData.allocateInterleaved(numEdges);
      this->outOfLineAllocateInterleaved(numNodes);
    }
    //std::cerr << "Done Alloc\n";
    for (size_t n = 0; n < numNodes; ++n) {
      nodeData.constructAt(n);
    }
    //std::cerr << "Done Node Construct\n";
    uint64_t cur = 0;
    for (size_t n = 0; n < numNodes; ++n) {
      cur += edgeNum(n);
      edgeIndData[n] = cur;
    }
    //std::cerr << "Done Edge Reserve\n";
    cur = 0;
    for (size_t n = 0; n < numNodes; ++n) {
      //if (n % (1024*128) == 0)
      //  std::cout << n << " " << cur << "\n";
      for (uint64_t e = 0, ee = edgeNum(n); e < ee; ++e) {
        if (EdgeData::has_value)
          edgeData.set(cur, _edgeData(n, e));
        edgeDst[cur] = _edgeDst(n, e);
        ++cur;
      }
    }

    //std::cerr << "Done Construct\n";
  }

  friend void swap(LC_CSR_Graph& lhs, LC_CSR_Graph& rhs) {
    swap(lhs.nodeData, rhs.nodeData);
    swap(lhs.edgeIndData, rhs.edgeIndData);
    swap(lhs.edgeDst, rhs.edgeDst);
    swap(lhs.edgeData, rhs.edgeData);
    std::swap(lhs.numNodes, rhs.numNodes);
    std::swap(lhs.numEdges, rhs.numEdges);
  }
  
  node_data_reference getData(GraphNode N, 
                              MethodFlag mflag = MethodFlag::WRITE) {
    // galois::runtime::checkWrite(mflag, false);
    NodeInfo& NI = nodeData[N];
    acquireNode(N, mflag);
    return NI.getData();
  }

  edge_data_reference getEdgeData(edge_iterator ni, 
                                  MethodFlag mflag = MethodFlag::UNPROTECTED) {
    // galois::runtime::checkWrite(mflag, false);
    return edgeData[*ni];
  }

  GraphNode getEdgeDst(edge_iterator ni) {
    return edgeDst[*ni];
  }

  size_t size() const { return numNodes; }
  size_t sizeEdges() const { return numEdges; }

  iterator begin() const { return iterator(0); }
  iterator end() const { return iterator(numNodes); }

  const_local_iterator local_begin() const { 
    return const_local_iterator(this->localBegin(numNodes));
  }

  const_local_iterator local_end() const { 
    return const_local_iterator(this->localEnd(numNodes));
  }

  local_iterator local_begin() { 
    return local_iterator(this->localBegin(numNodes));
  }

  local_iterator local_end() { 
    return local_iterator(this->localEnd(numNodes)); 
  }

  edge_iterator edge_begin(GraphNode N, MethodFlag mflag = MethodFlag::WRITE) {
    acquireNode(N, mflag);
    if (galois::runtime::shouldLock(mflag)) {
      for (edge_iterator ii = raw_begin(N), ee = raw_end(N); ii != ee; ++ii) {
        acquireNode(edgeDst[*ii], mflag);
      }
    }
    return raw_begin(N);
  }

  edge_iterator edge_end(GraphNode N, MethodFlag mflag = MethodFlag::WRITE) {
    acquireNode(N, mflag);
    return raw_end(N);
  }

  edge_iterator findEdge(GraphNode N1, GraphNode N2) {
    return std::find_if(edge_begin(N1), edge_end(N1), [=] (edge_iterator e) { 
      return getEdgeDst(e) == N2; 
    });
  }

  edge_iterator findEdgeSortedByDst(GraphNode N1, GraphNode N2) {
    auto e = std::lower_bound(edge_begin(N1), edge_end(N1), N2, 
                [=] (edge_iterator e, GraphNode N) { 
                  return getEdgeDst(e) < N; 
                });
    return (getEdgeDst(e) == N2) ? e : edge_end(N1);
  }

  runtime::iterable<NoDerefIterator<edge_iterator>> edges(GraphNode N, 
      MethodFlag mflag = MethodFlag::WRITE) {
    return internal::make_no_deref_range(edge_begin(N, mflag), edge_end(N, mflag));
  }

  runtime::iterable<NoDerefIterator<edge_iterator>> out_edges(GraphNode N, 
      MethodFlag mflag = MethodFlag::WRITE) {
    return edges(N, mflag);
  }

  /**
   * Sorts outgoing edges of a node. Comparison function is over EdgeTy.
   */
  template<typename CompTy>
  void sortEdgesByEdgeData(GraphNode N, 
                           const CompTy& comp = std::less<EdgeTy>(), 
                           MethodFlag mflag = MethodFlag::WRITE) {
    acquireNode(N, mflag);
    std::sort(edge_sort_begin(N), edge_sort_end(N), 
              internal::EdgeSortCompWrapper<EdgeSortValue<GraphNode,EdgeTy>,CompTy>(comp));
  }

  /**
   * Sorts outgoing edges of a node. 
   * Comparison function is over <code>EdgeSortValue<EdgeTy></code>.
   */
  template<typename CompTy>
  void sortEdges(GraphNode N, const CompTy& comp, 
                 MethodFlag mflag = MethodFlag::WRITE) {
    acquireNode(N, mflag);
    std::sort(edge_sort_begin(N), edge_sort_end(N), comp);
  }

  /**
   * Sorts outgoing edges of a node. Comparison is over getEdgeDst(e).
   */
  void sortEdgesByDst(GraphNode N, MethodFlag mflag = MethodFlag::WRITE) {
    acquireNode(N, mflag);
    typedef EdgeSortValue<GraphNode,EdgeTy> EdgeSortVal;
    std::sort(edge_sort_begin(N), edge_sort_end(N), 
      [=] (const EdgeSortVal& e1, const EdgeSortVal& e2) { 
        return e1.dst < e2.dst; 
      }
    );
  }

  /**
   * Sorts all outgoing edges of all nodes in parallel. Comparison is over 
   * getEdgeDst(e).
   */
  void sortAllEdgesByDst(MethodFlag mflag = MethodFlag::WRITE) {
    galois::do_all(galois::iterate(*this), [=] (GraphNode N) {
        this->sortEdgesByDst(N, mflag);
      },
      galois::steal<true>(),
      galois::no_stats());
  }

  typedef std::pair<iterator, iterator> NodeRange;
  typedef std::pair<edge_iterator, edge_iterator> EdgeRange;
  typedef std::pair<NodeRange, EdgeRange> GraphRange;

  /** 
   * Returns 2 ranges (one for nodes, one for edges) for a particular division.
   * The ranges specify the nodes/edges that a division is responsible for. The
   * function attempts to split them evenly among threads given some kind of
   * weighting
   *
   * @param nodeWeight weight to give to a node in division
   * @param edgeWeight weight to give to an edge in division
   * @param id Division number you want the ranges for
   * @param total Total number of divisions
   * @param edgePrefixSum a prefix sum of edges of the graph
   */
  template <typename VectorTy>
  auto divideByNode(size_t nodeWeight, size_t edgeWeight, size_t id, 
                    size_t total, VectorTy& edgePrefixSum)
      -> GraphRange {
    return 
      galois::graphs::divideNodesBinarySearch<VectorTy, uint32_t>(
        numNodes, numEdges, nodeWeight, edgeWeight, id, total, edgePrefixSum);
  }

  /** 
   * Returns 2 ranges (one for nodes, one for edges) for a particular division.
   * The ranges specify the nodes/edges that a division is responsible for. The
   * function attempts to split them evenly among threads given some kind of
   * weighting.
   *
   * Differs from above version as no prefix sum is passed in; however, you
   * can use the graph itself as a prefix sum array. Additionally, this
   * takes into account a node and edge offset since passing in the entire
   * graph as the prefix sum doesn't work if you only want to divide
   * a particular portion.
   *
   * @param nodeWeight weight to give to a node in division
   * @param edgeWeight weight to give to an edge in division
   * @param id Division number you want the ranges for
   * @param total Total number of divisions
   * @param nodesInRange Number of nodes in the range you want to divide
   * up (specified by node offset)
   * @param edgeInRange Number of edges in range you want to divide up
   * (specified by node offset)
   * @param nodeOffset Offset to the first node in the range you want
   * to divide up
   * @param edgeOffset Offset to the first edge in the range you want
   * to divide up
   */
  auto divideByNode(size_t nodeWeight, size_t edgeWeight, size_t id, 
                    size_t total, uint32_t nodesInRange, uint64_t edgesInRange,
                    uint32_t nodeOffset, uint64_t edgeOffset)
      -> GraphRange {
    std::vector<unsigned int> dummyScaleFactor;
    return 
      galois::graphs::divideNodesBinarySearch<LC_CSR_Graph, uint32_t>(
        nodesInRange, edgesInRange, nodeWeight, edgeWeight, id, total, *this,
        dummyScaleFactor, edgeOffset, nodeOffset);
  }

  /**
   * Returns the thread ranges array that specifies division of nodes among
   * threads 
   * 
   * @returns An array of uint32_t that specifies which thread gets which nodes.
   */
  const uint32_t* getThreadRanges() const {
    if (threadRanges.size() == 0) return nullptr;
    return threadRanges.data();
  }

  /**
   * Returns the thread ranges vector that specifies division of nodes among
   * threads 
   * 
   * @returns An vector of uint32_t that specifies which thread gets which 
   * nodes.
   */
  std::vector<uint32_t>& getThreadRangesVector() {
    return threadRanges;
  }

  /**
   * Helper function used by determineThreadRanges that consists of the main
   * loop over all threads and calls to divide by node to determine the 
   * division of nodes to threads.
   *
   * Saves the ranges to an argument vector provided by the caller.
   *
   * @param beginNode Beginning of range
   * @param endNode End of range, non-inclusive
   * @param returnRanges Vector to store thread offsets for ranges in
   * @param nodeAlpha The higher the number, the more weight nodes have in
   * determining division of nodes (edges have weight 1).
   */
  void determineThreadRangesThreadLoop(uint32_t beginNode, uint32_t endNode, 
                                     std::vector<uint32_t>& returnRanges, 
                                     uint32_t nodeAlpha) {
    uint32_t numNodesInRange = endNode - beginNode;
    uint64_t numEdgesInRange = edge_begin(endNode) - edge_begin(beginNode);
    uint32_t numThreads = galois::runtime::activeThreads;
    uint64_t edgeOffset = *edge_begin(beginNode);

    returnRanges[0] = beginNode;
    for (uint32_t i = 0; i < numThreads; i++) {
      // determine division for thread i
      auto nodeEdgeSplits = divideByNode(0, 1, i, numThreads, numNodesInRange,
                                         numEdgesInRange, beginNode, edgeOffset);

      auto nodeSplits = nodeEdgeSplits.first;

      // i.e. if there are actually assigned nodes
      if (nodeSplits.first != nodeSplits.second) {
        if (i != 0) {
          assert(returnRanges[i] == *(nodeSplits.first));
        } else { // i == 0
          assert(returnRanges[i] == 0);
        }
        returnRanges[i + 1] = *(nodeSplits.second) + beginNode;
      } else {
        // thread assinged no nodes
        returnRanges[i + 1] = returnRanges[i];
      }

      galois::gDebug("SaveVector: Thread ", i, " gets nodes ", returnRanges[i], 
                     " to ", returnRanges[i+1], ", num edges is ",
                     edge_begin(returnRanges[i + 1]) - 
                         edge_begin(returnRanges[i]));
    }
  }

  /**
   * Determines thread ranges for a given range of nodes and returns it as
   * an offset vector in the passed in vector. (thread ranges = assigned
   * nodes that a thread should work on)
   *
   * Checks for corner cases, then calls the main loop function.
   *
   * ONLY CALL AFTER GRAPH IS CONSTRUCTED as it uses functions that assume
   * the graph is already constructed.
   * 
   * @param beginNode Beginning of range
   * @param endNode End of range, non-inclusive
   * @param returnRanges Vector to store thread offsets for ranges in
   * @param nodeAlpha The higher the number, the more weight nodes have in
   * determining division of nodes (edges have weight 1).
   */
  void determineThreadRanges(uint32_t beginNode, uint32_t endNode, 
                             std::vector<uint32_t>& returnRanges, 
                             uint32_t nodeAlpha=0) {
    uint32_t numThreads = galois::runtime::activeThreads;
    uint32_t total_nodes = endNode - beginNode;

    returnRanges.resize(numThreads + 1);

    // check corner cases
    // no nodes = assign nothing to all threads
    if (beginNode == endNode) {
      returnRanges[0] = beginNode;
      for (uint32_t i = 0; i < numThreads; i++) {
        returnRanges[i+1] = beginNode;
      }
      return;
    }

    // single thread case; 1 thread gets all
    if (numThreads == 1) {
      returnRanges[0] = beginNode;
      returnRanges[1] = endNode;
      return;
    // more threads than nodes
    } else if (numThreads > total_nodes) {
      uint32_t current_node = beginNode;
      returnRanges[0] = current_node;
      // 1 node for threads until out of threads
      for (uint32_t i = 0; i < total_nodes; i++) {
        returnRanges[i+1] = ++current_node;
      }
      // deal with remainder threads; they get nothing
      for (uint32_t i = total_nodes; i < numThreads; i++) {
        returnRanges[i+1] = total_nodes;
      }
      return;
    }

    // no corner cases: onto main loop over nodes that determines
    // node ranges
    determineThreadRangesThreadLoop(beginNode, endNode, returnRanges, nodeAlpha);

    #ifndef NDEBUG
    // sanity checks
    assert(returnRanges[0] == beginNode && 
           "return ranges begin not the begin node");
    assert(returnRanges[numThreads] == endNode && 
           "return ranges end not end node");

    for (uint32_t i = 1; i < numThreads; i++) {
      assert(returnRanges[i] >= beginNode && returnRanges[i] <= endNode);
      assert(returnRanges[i] >= returnRanges[i-1]);
    }
    #endif
  }

  /**
   * Uses the divideByNode function (which is binary search based) to 
   * divide nodes/edges among threads.
   *
   * @param edgePrefixSum A prefix sum of edges
   */
  template <typename VectorTy>
  void determineThreadRangesByNode(VectorTy& edgePrefixSum) {
    uint32_t numThreads = galois::runtime::activeThreads;
    assert(numThreads > 0);

    if (threadRanges.size() != 0) {
      galois::gDebug("Warning: Thread ranges already specified "
                     "(in detThreadRangesByNode)");
    }

    if (threadRangesEdge.size() != 0) {
      galois::gDebug("Warning: Thread ranges edge already specified "
                     "(in detThreadRangesByNode)");
    }

    clearRanges();
    threadRanges.resize(numThreads + 1);
    threadRangesEdge.resize(numThreads + 1);

    threadRanges[0] = 0;
    threadRangesEdge[0] = 0;

    for (uint32_t i = 0; i < numThreads; i++) {
      auto nodeEdgeSplits = divideByNode(0, 1, i, numThreads, edgePrefixSum);
      auto nodeSplits = nodeEdgeSplits.first;
      auto edgeSplits = nodeEdgeSplits.second;

      // i.e. if there are actually assigned nodes
      if (nodeSplits.first != nodeSplits.second) {
        if (i != 0) {
          assert(threadRanges[i] == *(nodeSplits.first));
          assert(threadRangesEdge[i] == *(edgeSplits.first));
        } else { // i == 0
          assert(threadRanges[i] == 0);
          assert(threadRangesEdge[i] == 0);
        }

        threadRanges[i + 1] = *(nodeSplits.second);
        threadRangesEdge[i + 1] = *(edgeSplits.second);
      } else {
        // thread assinged no nodes
        assert(edgeSplits.first == edgeSplits.second);
        threadRanges[i + 1] = threadRanges[i];
        threadRangesEdge[i + 1] = threadRangesEdge[i];
      }

      galois::gDebug("Thread ", i, " gets nodes ", threadRanges[i], " to ", 
                     threadRanges[i+1]);
      galois::gDebug("Thread ", i, " gets edges ", threadRangesEdge[i], " to ", 
                     threadRangesEdge[i+1]);
    }
  }


  template <typename F>
  ptrdiff_t partition_neighbors(GraphNode N, const F& func) {
    auto beg = &edgeDst[*raw_begin (N)];
    auto end = &edgeDst[*raw_end (N)];
    auto mid = std::partition (beg, end, func);
    return (mid - beg);
  }

  void allocateFrom(FileGraph& graph) {
    numNodes = graph.size();
    numEdges = graph.sizeEdges();
    if (UseNumaAlloc) {
      nodeData.allocateBlocked(numNodes);
      edgeIndData.allocateBlocked(numNodes);
      edgeDst.allocateBlocked(numEdges);
      edgeData.allocateBlocked(numEdges);
      this->outOfLineAllocateBlocked(numNodes);
    } else {
      nodeData.allocateInterleaved(numNodes);
      edgeIndData.allocateInterleaved(numNodes);
      edgeDst.allocateInterleaved(numEdges);
      edgeData.allocateInterleaved(numEdges);
      this->outOfLineAllocateInterleaved(numNodes);
    }
  }

  void allocateFrom(uint32_t nNodes, uint64_t nEdges) {
    numNodes = nNodes;
    numEdges = nEdges;

    if (UseNumaAlloc) {
      nodeData.allocateLocal(numNodes);
      edgeIndData.allocateLocal(numNodes);
      edgeDst.allocateLocal(numEdges);
      edgeData.allocateLocal(numEdges);
      this->outOfLineAllocateLocal(numNodes);
    } else {
      nodeData.allocateInterleaved(numNodes);
      edgeIndData.allocateInterleaved(numNodes);
      edgeDst.allocateInterleaved(numEdges);
      edgeData.allocateInterleaved(numEdges);
      this->outOfLineAllocateInterleaved(numNodes);
    }
  }

  /**
   * A version of allocateFrom that takes into account node/edge distribution
   * and tries to make the allocation of memory more numa aware.
   * Based on divideByNode in Lonestar.
   *
   * @param nNodes Number to allocate for node data
   * @param nEdges Number to allocate for edge data
   * @param edgePrefixSum Vector with prefix sum of edges.
   */
  void allocateFromByNode(uint32_t nNodes, uint64_t nEdges, 
                          std::vector<uint64_t> edgePrefixSum) {
    numNodes = nNodes;
    numEdges = nEdges;

    // gets both threadRanges + threadRangesEdge
    determineThreadRangesByNode(edgePrefixSum);

    // node based alloc
    nodeData.allocateSpecified(numNodes, threadRanges);
    edgeIndData.allocateSpecified(numNodes, threadRanges);

    // edge based alloc
    edgeDst.allocateSpecified(numEdges, threadRangesEdge);
    edgeData.allocateSpecified(numEdges, threadRangesEdge);

    this->outOfLineAllocateSpecified(numNodes, threadRanges);
  }

  void constructNodes() {
#ifndef GALOIS_GRAPH_CONSTRUCT_SERIAL
    for (uint32_t x = 0; x < numNodes; ++x) {
      nodeData.constructAt(x);
      this->outOfLineConstructAt(x);
    }
#else
    galois::do_all(galois::iterate(0ul, numNodes), [&](uint32_t x) {
                     nodeData.constructAt(x);
                     this->outOfLineConstructAt(x);
                   }, 
                   galois::loopname("CONSTRUCT_NODES"), 
                   galois::no_stats());
#endif
  }

  void constructEdge(uint64_t e, uint32_t dst, 
                     const typename EdgeData::value_type& val) {
    edgeData.set(e, val);
    edgeDst[e] = dst;
  }

  void constructEdge(uint64_t e, uint32_t dst) {
    edgeDst[e] = dst;
  }

  void fixEndEdge(uint32_t n, uint64_t e) {
    edgeIndData[n] = e;
  }

  /** 
   * Perform an in-memory transpose of the graph, replacing the original
   * CSR to CSC
   */
  void transpose(bool reallocate = false) {
    galois::StatTimer timer("TIME_GRAPH_TRANSPOSE"); timer.start();

    EdgeDst edgeDst_old;
    EdgeData edgeData_new;
    EdgeIndData edgeIndData_old;
    EdgeIndData edgeIndData_temp;

    edgeIndData_old.allocateInterleaved(numNodes);
    edgeIndData_temp.allocateInterleaved(numNodes);
    edgeDst_old.allocateInterleaved(numEdges);
    edgeData_new.allocateInterleaved(numEdges);

    // Copy old node->index location + initialize the temp array
    galois::do_all(galois::iterate(0ul, numNodes), [&](uint32_t n) {
                     edgeIndData_old[n] = edgeIndData[n];
                     edgeIndData_temp[n] = 0;
                   }, 
                   galois::loopname("TRANSPOSE_EDGEINTDATA_COPY"), 
                   galois::no_stats());

    // parallelization makes this slower
    // get destination of edge, copy to array, and  
    for (uint64_t e = 0; e < numEdges; ++e) { 
        auto dst = edgeDst[e];
        edgeDst_old[e] = dst;
        // counting outgoing edges in the tranpose graph by
        // counting incoming edges in the original graph
        ++edgeIndData_temp[dst];
    }

    // prefix sum calculation of the edge index array
    for (uint32_t n = 1; n < numNodes; ++n) {
      edgeIndData_temp[n] += edgeIndData_temp[n-1];
    }

    // recalculate thread ranges for nodes and edges
    clearRanges();
    determineThreadRangesByNode(edgeIndData_temp);

    // reallocate nodeData
    if (reallocate) {
      nodeData.deallocate();
      nodeData.allocateSpecified(numNodes, threadRanges);
    }

    // reallocate edgeIndData
    if (reallocate) {
      edgeIndData.deallocate();
      edgeIndData.allocateSpecified(numNodes, threadRanges);
    }

    // copy over the new tranposed edge index data
    galois::do_all(galois::iterate(0ul, numNodes), [&](uint32_t n) {
                     edgeIndData[n] = edgeIndData_temp[n];
                   }, 
                   galois::loopname("TRANSPOSE_EDGEINTDATA_SET"), 
                   galois::no_stats());

    // edgeIndData_temp[i] will now hold number of edges that all nodes
    // before the ith node have
    if (numNodes >= 1) {
      edgeIndData_temp[0] = 0;
      galois::do_all(galois::iterate(1ul, numNodes), [&](uint32_t n) {
                       edgeIndData_temp[n] = edgeIndData[n-1];
                     }, 
                     galois::loopname("TRANSPOSE_EDGEINTDATA_TEMP"), 
                     galois::no_stats());
    }

    // reallocate edgeDst
    if (reallocate) {
      edgeDst.deallocate();
      edgeDst.allocateSpecified(numEdges, threadRangesEdge);
    }

    // parallelization makes this slower
    for (uint32_t src = 0; src < numNodes; ++src) { 
      // e = start index into edge array for a particular node
      uint64_t e;
      if (src == 0) 
        e = 0;
      else 
        e = edgeIndData_old[src - 1];

      // get all outgoing edges of a particular node in the non-transpose and
      // convert to incoming
      while (e < edgeIndData_old[src]) {
        // destination nodde
        auto dst = edgeDst_old[e];
        // location to save edge
        auto e_new = edgeIndData_temp[dst]++;
        // save src as destination
        edgeDst[e_new] = src;
        // copy edge data to "new" array
        edgeDataCopy(edgeData_new, edgeData, e_new, e);
        e++;
      }
    }

    // reallocate edgeData
    if (reallocate) {
      edgeData.deallocate();
      edgeData.allocateSpecified(numEdges, threadRangesEdge);
    }

    // if edge weights, then overwrite edgeData with new edge data
    if (EdgeData::has_value) {
      galois::do_all(galois::iterate(0ul, numEdges),
                     [&](uint32_t e){
                       edgeDataCopy(edgeData, edgeData_new, e, e);
                     }, 
                     galois::loopname("TRANSPOSE_EDGEDATA_SET"), 
                     galois::no_stats());
    }

    timer.stop();
  }

  template<bool is_non_void = EdgeData::has_value>
  void edgeDataCopy(EdgeData &edgeData_new, EdgeData &edgeData, 
                    uint64_t e_new, uint64_t e, 
                    typename std::enable_if<is_non_void>::type* = 0) {
    edgeData_new[e_new] = edgeData[e];
  }

  template<bool is_non_void = EdgeData::has_value>
  void edgeDataCopy(EdgeData &edgeData_new, EdgeData &edgeData, 
                    uint64_t e_new, uint64_t e, 
                    typename std::enable_if<!is_non_void>::type* = 0) {
    // does nothing
  }


  void constructFrom(FileGraph& graph, unsigned tid, unsigned total) {
    // at this point memory should already be allocated
    auto r = graph.divideByNode(
        NodeData::size_of::value + EdgeIndData::size_of::value + 
        LC_CSR_Graph::size_of_out_of_line::value,
        EdgeDst::size_of::value + EdgeData::size_of::value,
        tid, total).first;

    this->setLocalRange(*r.first, *r.second);

    for (FileGraph::iterator ii = r.first, ei = r.second; ii != ei; ++ii) {
      nodeData.constructAt(*ii);
      edgeIndData[*ii] = *graph.edge_end(*ii);

      this->outOfLineConstructAt(*ii);

      for (FileGraph::edge_iterator nn = graph.edge_begin(*ii), 
                                    en = graph.edge_end(*ii); 
           nn != en;
           ++nn) {
        constructEdgeValue(graph, nn);
        edgeDst[*nn] = graph.getEdgeDst(nn);
      }
    }
  }
};
} // end namespace
} // end namespace

#endif
