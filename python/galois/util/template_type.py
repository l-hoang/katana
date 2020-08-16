from functools import wraps

import numpy as np


class TemplateType1(type):
    def __new__(cls, name, _bases, attrs):
        instantiations = {np.dtype(t): v for t, v in attrs["instantiations"].items()}
        del attrs["instantiations"]
        representative_type = next(iter(instantiations.values()))
        attrs.update(representative_type.__dict__)
        attrs["__instantiations__"] = instantiations
        attrs["__representative__"] = representative_type

        @wraps(representative_type.__init__)
        def _init_stub(self, *_args, **_kwargs):
            name = type(self).__name__
            raise TypeError("{0} cannot be instantiated directly. Select a specific type with {0}[...].".format(name))

        attrs["__init__"] = _init_stub
        return type.__new__(cls, name, (), attrs)

    def __getitem__(cls, item):
        return cls.__instantiations__[np.dtype(item)]

    def __instancecheck__(cls, instance):
        return any(isinstance(instance, ty) for ty in cls.__instantiations__.values())

    def __subclasscheck__(cls, subclass):
        return any(issubclass(subclass, ty) for ty in cls.__instantiations__.values())

    def __repr__(cls):
        return "<template class '{}.{}'>".format(cls.__representative__.__module__, cls.__qualname__)


def make_template_type1(name, instantiations):
    return TemplateType1(name, None, dict(instantiations=instantiations))
