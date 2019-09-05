from typing import Union 
import numpy as np
from .lib.xiuli import XSkeleton
import struct
from io import BytesIO


class Skeleton(XSkeleton):
    """Neuron skeleton 
    
    Parameters
    ----------
    nodes: (float ndarray, node_num x 4), each row is a node with r,z,y,x 
    parents: (int ndarray, node_num), the parent node index of each node
    types: (int ndarray, node_num), the type of each node.
        The type of node is defined in `SWC format
        <http://www.neuronland.org/NLMorphologyConverter/MorphologyFormats/SWC/Spec.html>`_:

        0 - undefined
        1 - soma
        2 - axon
        3 - (basal) dendrite
        4 - apical dendrite
        5 - fork point
        6 - end point
        7 - custom
    """
    def __init__(self, *args): 
        super().__init__(*args)
   
    @classmethod
    def from_nodes_and_parents(cls, nodes: np.ndarray, parents: np.ndarray, 
                    classes: np.ndarray=None):
        assert nodes.shape[1] == 4
        assert nodes.shape[0] == len(parents) == len(classes)

        node_num = nodes.shape[0]
        nodes = nodes.astype(np.float32) 

        attributes = np.zeros((node_num, 4), dtype=np.int32) - 2
        # the parents, first child and siblings should be missing initially. 
        # The zero will all point to the first node.
        if classes is not None:
            attributes[:, 0] = classes
        else:
            # default should be undefined
            attributes[:, 0] = 0

        attributes[:, 1] = parents

        return cls(nodes, attributes) 

    @classmethod
    def from_swc(cls, file_name: str):
        """
        Parameters:
        ------------
        file_name: the swc file path
        sort_id: The node index could be unsorted in some swc files, 
            we can drop the node index column after order it. Our future
            analysis assumes that the nodes are ordered.
        """
        # numpy load text is faster than my c++ implementation!
        # it might use memory map internally
        swc_array = np.loadtxt(file_name, dtype=np.float32)
        return cls( swc_array )

    def to_swc(self, file_name: str, precision: int = 3):
        """
        Parameters
        -----------
        file_name: swc file name.
        precision: the digits used to write float number. If you are using nanometer as unit, it is recommended to use precision 0. If you are using micron as unit, it is recommended to use precision 3.
        """
        self.write_swc(file_name, precision)

    @classmethod
    def from_precomputed(cls, skelbuf):
        """
        Convert a buffer into a Skeleton object
        This function is modified from cloud-volume

        Format:
        num vertices (Nv) (uint32)
        num edges (Ne) (uint32)
        XYZ x Nv (float32)
        edge x Ne (2x uint32)

        Default Vertex Attributes:

            radii x Nv (optional, float32)
            vertex_type x Nv (optional, req radii, uint8) (SWC definition)
        """
        if len(skelbuf) < 8:
            raise ValueError("{} bytes is fewer than needed to specify the number of verices and edges.".format(len(skelbuf)))

        num_vertices, num_edges = struct.unpack('<II', skelbuf[:8])
        min_format_length = 8 + 12 * num_vertices + 8 * num_edges

        if len(skelbuf) < min_format_length:
            raise ValueError("The input skeleton was {} bytes but the format requires {} bytes.".format(len(skelbuf), min_format_length))

        vstart = 2 * 4 # two uint32s in
        vend = vstart + num_vertices * 3 * 4 # float32s
        vertbuf = skelbuf[ vstart : vend ]

        estart = vend
        eend = estart + num_edges * 4 * 2 # 2x uint32s
        edgebuf = skelbuf[ estart : eend ]

        vertices = np.frombuffer(vertbuf, dtype='<f4').reshape( (num_vertices, 3) )
        edges = np.frombuffer(edgebuf, dtype='<u4').reshape( (num_edges, 2) )
        parents = np.zeros(num_vertices, dtype=np.int32) - 2
        # the first one is child, the second one is parent
        parents[ edges[:, 0] ] = edges[:, 1]

        radii = None
        classes = None
        if len(skelbuf) >= min_format_length + num_vertices * 4:
            # there is radii information
            radii_start = eend
            radii_end = radii_start + num_vertices*4
            radii = np.frombuffer(skelbuf[radii_start : radii_end], dtype=np.float32)

        if len(skelbuf) >= min_format_length + num_vertices * 5:
            # there is node classes information
            classes_start = radii_end
            classes_end = classes_start + num_vertices
            classes = np.frombuffer(skelbuf[classes_start : classes_end], dtype=np.uint8)

        if radii is None: 
            assert len(skelbuf) == min_format_length
            radii = np.zeros(num_vertices, dtype=np.float32 )

        if classes is None:
            assert len(skelbuf) <= min_format_length + num_vertices * 4
            classes = np.zeros(num_vertices, dtype=np.int32 )

        nodes = np.column_stack((vertices, radii))
        return cls.from_nodes_and_parents(nodes, parents, classes)

    def to_precomputed(self):
        nodes = self.nodes.astype(np.float32)
        node_num = self.nodes.shape[0]
        classes = self.attributes[:, 0].astype(np.uint8)
        edges = self.edges.astype( np.uint32 )
        edge_num = edges.shape[0]

        result = BytesIO()
        result.write(struct.pack('<II', node_num, edge_num))
        result.write( nodes[:, :3].tobytes('C') )
        result.write( edges.tobytes('C') )

        # write radii
        radii = nodes[:, 3]
        if not np.ma.allequal(radii, 0) or not np.ma.allequal(classes, 0):
            result.write( nodes[:, 3].tobytes('C') )
        
        # write node types
        if not np.ma.allequal(classes, 0):
            result.write( classes.tobytes('C') )
        return result.getvalue()



    def __eq__(self, other):
        assert isinstance( other, Skeleton )
        return  np.ma.allclose(self.nodes, other.nodes, atol=0.001) and np.ma.allequal( 
                                    self.attributes, other.attributes )