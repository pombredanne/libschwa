
from unittest import TestCase
from StringIO import StringIO
from schwa import dr

class DocWithoutFields(dr.Document):
    pass

class DocWithField(dr.Document):
    field = dr.Field()

class Annot(dr.Annotation):
    pass

class DocWithAnnotsAndPointer(dr.Document):
    annots = dr.Annotations('Annot')
    special_annot = dr.Pointer('Annot')

def write_x_read_y(doc, class2):
    """typecast doc as class2"""
    print 'Writing {0}'.format(doc.__class__.__name__)
    f = StringIO()
    dr.Writer(f).write_doc(doc)
    f.seek(0)
    print 'Reading {0}'.format(class2.__name__)
    return dr.Reader(class2).stream(f).next()

class CastTestCase(TestCase):
    def test_various(self):
        doc = DocWithField()
        doc = write_x_read_y(doc, DocWithField)
        doc.field = 'foo'
        doc = write_x_read_y(doc, DocWithoutFields)
        doc = write_x_read_y(doc, DocWithAnnotsAndPointer)
        doc.annots.create()
        doc.special_annot = doc.annots[-1]
        doc = write_x_read_y(doc, DocWithoutFields)
        doc = write_x_read_y(doc, DocWithField)
        self.assertEquals(doc.field, 'foo')
        self.assertEquals(len(doc.annots), 1)
        self.assertEquals(doc.special_annot, doc.annots[0])
