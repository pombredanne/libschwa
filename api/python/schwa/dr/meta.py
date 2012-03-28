from .exceptions import DependencyException
from .fields import *
from .utils import pluralise

__all__ = ['AnnotationMeta', 'Annotation', 'Document', 'Token']


class DocrepMeta(type):
  def __new__(mklass, klass_name, bases, attrs):
    meta = attrs.get('Meta', None)

    # construct the class
    klass = super(DocrepMeta, mklass).__new__(mklass, klass_name, bases, attrs)

    # discover the BaseAnnotationField and BaseAnnotationsField instances
    annotations, fields, s2p = {}, {}, {}
    for base in bases:
      annotations.update(getattr(base, '_dr_annotations', {}))
      fields.update(getattr(base, '_dr_fields', {}))
      s2p.update(getattr(base, '_dr_s2p', {}))
    for name, field in attrs.iteritems():
      if isinstance(field, BaseField):
        if isinstance(field, BaseAnnotationsField):
          if field.sname is None:
            field.sname = field.klass_name
          annotations[name] = field
        elif isinstance(field, BaseAnnotationField):
          if field.sname is None:
            field.sname = name
          fields[name] = field
          s2p[field.sname] = name

    # adds the Field and Annotation information appropriately
    klass._dr_fields = fields            # { pyname : Field }
    klass._dr_annotations = annotations  # { pyname : Annotations }
    klass._dr_s2p = s2p                  # { sname : pyname }

    # add the name
    if hasattr(meta, 'name'):
      klass._dr_name = meta.name
    else:
      klass._dr_name = klass_name.split('.')[-1]

    # add the plural name
    if hasattr(meta, 'plural'):
      klass._dr_plural = meta.plural
    else:
      klass._dr_plural = pluralise(klass._dr_name)

    # add the dependency requirements fulfilled flag
    klass._dr_fulfilled = False

    return klass


class AnnotationMeta(DocrepMeta):
  reg     = {} # { name : ( sorted(attrs), klass) }
  unbound = {} # { name : [ (Field, klass) ] }

  def __new__(mklass, klass_name, bases, attrs):
    klass = super(AnnotationMeta, mklass).__new__(mklass, klass_name, bases, attrs)
    AnnotationMeta.register(klass)
    return klass


  @staticmethod
  def register(klass):
    fields = tuple(sorted(klass._dr_fields.keys() + klass._dr_annotations.keys()))
    name = klass._dr_name

    # check if we have cached this class
    if name in AnnotationMeta.reg:
      f, k = AnnotationMeta.reg[name]
      if fields != f:
        raise ValueError('Cannot register two Annotation types {0!r} with the same name but with different fields ({1} != {2})'.format(name, f, fields))
      del klass
      return k

    # register the class
    AnnotationMeta.reg[name] = (fields, klass)

    # update the dependency fulfilled information for the class
    for field_set in ('_dr_fields', '_dr_annotations'):
      for field in getattr(klass, field_set).itervalues():
        if not field.is_fulfilled():
          dep = field.get_dependency()
          if dep in AnnotationMeta.reg:
            _, k = AnnotationMeta.reg[dep]
            field.set_dependency(k)
          else:
            if dep not in AnnotationMeta.unbound:
              AnnotationMeta.unbound[dep] = []
            AnnotationMeta.unbound[dep].append( (field, klass) )
    klass.update_fulfilled()

    # update fields which depend on this newly created class
    if name in AnnotationMeta.unbound:
      for f, k in AnnotationMeta.unbound[name]:
        f.set_dependency(klass)
        k.update_fulfilled()
      del AnnotationMeta.unbound[name]


  @staticmethod
  def cached(klass_name):
    x = AnnotationMeta.reg.get(klass_name)
    return x and x[1]


class Base(object):
  __metaclass__ = AnnotationMeta

  def __init__(self, **kwargs):
    if not self._dr_fulfilled:
      raise DependencyException('Cannot instantiate class {0!r} with unfilled dependencies {1!r}'.format(self.__class__.__name__, list(self.find_unfulfilled())))

    for name, field in self._dr_fields.iteritems():
      self.__dict__[name] = field.default()
    for name, annotations in self._dr_annotations.iteritems():
      self.__dict__[name] = annotations.default()

    for k, v in kwargs.iteritems():
      setattr(self, k, v)

  FIELD_MSG_TEMPLATE = 'Field "{0}" which refers to class name "{1}"'
  @classmethod
  def find_unfulfilled(klass):
    """Returns an unfulfilled field where available, otherwise None"""
    for name, field in klass._dr_fields.iteritems():
      if not field.is_fulfilled():
        if hasattr(field, 'klass_name'):
            yield klass.FIELD_MSG_TEMPLATE.format(name, field.klass_name)
        else:
            yield name
    for name, field in klass._dr_annotations.iteritems():
      if not field.is_fulfilled():
        if hasattr(field, 'klass_name'):
            yield klass.FIELD_MSG_TEMPLATE.format(name, field.klass_name)
        else:
            yield name

  @classmethod
  def update_fulfilled(klass):
    klass._dr_fulfilled = not any(klass.find_unfulfilled())

  @classmethod
  def update_fields(klass, fields):
    a = frozenset(klass._dr_s2p)
    b = frozenset(fields)
    for name in b - a:
      klass._dr_fields[name] = fields[name]
      klass._dr_s2p[name] = name


class Annotation(Base):
  class Meta:
    name = 'schwa.BaseAnnotation'


class Document(Base):
  class Meta:
    name = 'schwa.BaseDocument'

  def ready(self):
    """ Hook called after a Document and all its Annotations are loaded. """
    pass


class Token(Annotation):
  span = Range()
  raw  = Field()
  norm = Field()

  class Meta:
    name = 'schwa.BaseToken'

  def __repr__(self):
    return 'Token({0!r})'.format(self.norm)

  def __str__(self):
    return self.norm

