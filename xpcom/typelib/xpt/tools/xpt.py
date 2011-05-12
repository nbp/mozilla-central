#!/usr/bin/env python
# Copyright 2010,2011 Mozilla Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#   1. Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in
#      the documentation and/or other materials provided with the
#      distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE MOZILLA FOUNDATION ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE MOZILLA FOUNDATION OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# The views and conclusions contained in the software and documentation
# are those of the authors and should not be interpreted as representing
# official policies, either expressed or implied, of the Mozilla
# Foundation.

"""
A module for working with XPCOM Type Libraries.

The XPCOM Type Library File Format is described at:
http://www.mozilla.org/scriptable/typelib_file.html . It is used
to provide type information for calling methods on XPCOM objects
from scripting languages such as JavaScript.

This module provides a set of classes representing the parts of
a typelib in a high-level manner, as well as methods for reading
and writing them from files.

The usable public interfaces are currently:
Typelib.read(filename) - read a typelib from a file on disk, return
                         a Typelib object.

xpt_dump(filename)     - read a typelib from a file on disk, dump
                         the contents to stdout in a human-readable
                         format.

Typelib()              - construct a new Typelib object
Interface()            - construct a new Interface object
Method()               - construct a new object representing a method
                         defined on an Interface
Constant()             - construct a new object representing a constant
                         defined on an Interface
Param()                - construct a new object representing a parameter
                         to a method
SimpleType()           - construct a new object representing a simple
                         data type
InterfaceType()        - construct a new object representing a type that
                         is an IDL-defined interface

"""

from __future__ import with_statement
import os, sys
import struct

# header magic
XPT_MAGIC = "XPCOM\nTypeLib\r\n\x1a"
TYPELIB_VERSION = (1, 2)

class FileFormatError(Exception):
    pass

class DataError(Exception):
    pass

# Magic for creating enums
def M_add_class_attribs(attribs):
    def foo(name, bases, dict_):
        for v, k in attribs:
            dict_[k] = v
        return type(name, bases, dict_)
    return foo

def enum(*names):
    class Foo(object):
        __metaclass__ = M_add_class_attribs(enumerate(names))
        def __setattr__(self, name, value):  # this makes it read-only
            raise NotImplementedError
    return Foo()

# Descriptor types as described in the spec
class Type(object):
    """
    Data type of a method parameter or return value. Do not instantiate
    this class directly. Rather, use one of its subclasses.
    
    """
    _prefixdescriptor = struct.Struct(">B")
    Tags = enum(
        # The first 18 entries are SimpleTypeDescriptor
        'int8',
        'int16',
        'int32',
        'int64',
        'uint8',
        'uint16',
        'uint32',
        'uint64',
        'float',
        'double',
        'boolean',
        'char',
        'wchar_t',
        'void',
        # the following four values are only valid as pointers
        'nsIID',
        'DOMString',
        'char_ptr',
        'wchar_t_ptr',
        # InterfaceTypeDescriptor
        'Interface',
        # InterfaceIsTypeDescriptor
        'InterfaceIs',
        # ArrayTypeDescriptor
        'Array',
        # StringWithSizeTypeDescriptor
        'StringWithSize',
        # WideStringWithSizeTypeDescriptor
        'WideStringWithSize',
        #XXX: These are also SimpleTypes (but not in the spec)
        # http://hg.mozilla.org/mozilla-central/annotate/0e0e2516f04e/xpcom/typelib/xpt/tools/xpt_dump.c#l69
        'UTF8String',
        'CString',
        'AString',
        'jsval',
        )

    def __init__(self, pointer=False, unique_pointer=False, reference=False):
        self.pointer = pointer
        self.unique_pointer = unique_pointer
        self.reference = reference

    @staticmethod
    def decodeflags(byte):
        """
        Given |byte|, an unsigned uint8 containing flag bits,
        decode the flag bits as described in
        http://www.mozilla.org/scriptable/typelib_file.html#TypeDescriptor
        and return a dict of flagname: (True|False) suitable
        for passing to Type.__init__ as **kwargs.
        
        """
        return {'pointer': bool(byte & 0x80),
                'unique_pointer': bool(byte & 0x40),
                'reference': bool(byte & 0x20),
                }

    def encodeflags(self):
        """
        Encode the flag bits of this Type object. Returns a byte.

        """
        flags = 0
        if self.pointer:
            flags |= 0x80
        if self.unique_pointer:
            flags |= 0x40
        if self.reference:
            flags |= 0x20
        return flags
    
    @staticmethod
    def read(typelib, map, data_pool, offset):
        """
        Read a TypeDescriptor at |offset| from the mmaped file |map| with
        data pool offset |data_pool|. Returns (Type, next offset),
        where |next offset| is an offset suitable for reading the data
        following this TypeDescriptor.
        
        """
        start = data_pool + offset - 1
        (data,) = Type._prefixdescriptor.unpack(map[start:start + Type._prefixdescriptor.size])
        # first three bits are the flags
        flags = data & 0xE0
        flags = Type.decodeflags(flags)
        # last five bits is the tag
        tag = data & 0x1F
        offset += Type._prefixdescriptor.size
        t = None
        if tag <= Type.Tags.wchar_t_ptr or tag >= Type.Tags.UTF8String:
            t = SimpleType.get(data, tag, flags)
        elif tag == Type.Tags.Interface:
            t, offset = InterfaceType.read(typelib, map, data_pool, offset, flags)
        elif tag == Type.Tags.InterfaceIs:
            t, offset = InterfaceIsType.read(typelib, map, data_pool, offset, flags)
        elif tag == Type.Tags.Array:
            t, offset = ArrayType.read(typelib, map, data_pool, offset, flags)
        elif tag == Type.Tags.StringWithSize:
            t, offset = StringWithSizeType.read(typelib, map, data_pool, offset, flags)
        elif tag == Type.Tags.WideStringWithSize:
            t, offset = WideStringWithSizeType.read(typelib, map, data_pool, offset, flags)
        return t, offset

    def write(self, typelib, file):
        """
        Write a TypeDescriptor to |file|, which is assumed
        to be seeked to the proper position. For types other than
        SimpleType, this is not sufficient for writing the TypeDescriptor,
        and the subclass method must be called.

        """
        file.write(Type._prefixdescriptor.pack(self.encodeflags() | self.tag))

class SimpleType(Type):
    """
    A simple data type. (SimpleTypeDescriptor from the typelib specification.)

    """
    _cache = {}

    def __init__(self, tag, **kwargs):
        Type.__init__(self, **kwargs)
        self.tag = tag

    @staticmethod
    def get(data, tag, flags):
        """
        Get a SimpleType object representing |data| (a TypeDescriptorPrefix).
        May return an already-created object. If no cached object is found,
        construct one with |tag| and |flags|.
        
        """
        if data not in SimpleType._cache:
            SimpleType._cache[data] = SimpleType(tag, **flags)
        return SimpleType._cache[data]

    def __str__(self):
        s = "unknown"
        if self.tag == Type.Tags.char_ptr and self.pointer:
            return "string"
        if self.tag == Type.Tags.wchar_t_ptr and self.pointer:
            return "wstring"
        for t in dir(Type.Tags):
            if self.tag == getattr(Type.Tags, t):
                s = t
                break

        if self.pointer:
            if self.reference:
                s += " &"
            else:
                s += " *"
        return s

class InterfaceType(Type):
    """
    A type representing a pointer to an IDL-defined interface.
    (InterfaceTypeDescriptor from the typelib specification.)

    """
    _descriptor = struct.Struct(">H")

    def __init__(self, iface, pointer=True, **kwargs):
        if not pointer:
            raise DataError, "InterfaceType is not valid with pointer=False"
        Type.__init__(self, pointer=pointer, **kwargs)
        self.iface = iface
        self.tag = Type.Tags.Interface

    @staticmethod
    def read(typelib, map, data_pool, offset, flags):
        """
        Read an InterfaceTypeDescriptor at |offset| from the mmaped
        file |map| with data pool offset |data_pool|.
        Returns (InterfaceType, next offset),
        where |next offset| is an offset suitable for reading the data
        following this InterfaceTypeDescriptor.
        
        """
        if not flags['pointer']:
            return None, offset
        start = data_pool + offset - 1
        (iface_index,) = InterfaceType._descriptor.unpack(map[start:start + InterfaceType._descriptor.size])
        offset += InterfaceType._descriptor.size
        iface = None
        # interface indices are 1-based
        if iface_index > 0 and iface_index <= len(typelib.interfaces):
            iface = typelib.interfaces[iface_index - 1]
        return InterfaceType(iface, **flags), offset

    def write(self, typelib, file):
        """
        Write an InterfaceTypeDescriptor to |file|, which is assumed
        to be seeked to the proper position.

        """
        Type.write(self, typelib, file)
        # write out the interface index (1-based)
        file.write(InterfaceType._descriptor.pack(typelib.interfaces.index(self.iface) + 1))

    def __str__(self):
        if self.iface:
            return self.iface.name
        return "unknown interface"

class InterfaceIsType(Type):
    """
    A type representing an interface described by one of the other
    arguments to the method. (InterfaceIsTypeDescriptor from the
    typelib specification.)
    
    """
    _descriptor = struct.Struct(">B")
    _cache = {}

    def __init__(self, param_index, pointer=True, **kwargs):
        if not pointer:
            raise DataError, "InterfaceIsType is not valid with pointer=False"
        Type.__init__(self, pointer=pointer, **kwargs)
        self.param_index = param_index
        self.tag = Type.Tags.InterfaceIs

    @staticmethod
    def read(typelib, map, data_pool, offset, flags):
        """
        Read an InterfaceIsTypeDescriptor at |offset| from the mmaped
        file |map| with data pool offset |data_pool|.
        Returns (InterfaceIsType, next offset),
        where |next offset| is an offset suitable for reading the data
        following this InterfaceIsTypeDescriptor.
        May return a cached value.
        
        """
        if not flags['pointer']:
            return None, offset
        start = data_pool + offset - 1
        (param_index,) = InterfaceIsType._descriptor.unpack(map[start:start + InterfaceIsType._descriptor.size])
        offset += InterfaceIsType._descriptor.size
        if param_index not in InterfaceIsType._cache:
            InterfaceIsType._cache[param_index] = InterfaceIsType(param_index, **flags)
        return InterfaceIsType._cache[param_index], offset

    def write(self, typelib, file):
        """
        Write an InterfaceIsTypeDescriptor to |file|, which is assumed
        to be seeked to the proper position.

        """
        Type.write(self, typelib, file)
        file.write(InterfaceIsType._descriptor.pack(self.param_index))

    def __str__(self):
        return "InterfaceIs *"

class ArrayType(Type):
    """
    A type representing an Array of elements of another type, whose
    size and length are passed as separate parameters to a method.
    (ArrayTypeDescriptor from the typelib specification.)
    
    """
    _descriptor = struct.Struct(">BB")

    def __init__(self, element_type, size_is_arg_num, length_is_arg_num,
                 pointer=True, **kwargs):
        if not pointer:
            raise DataError, "ArrayType is not valid with pointer=False"
        Type.__init__(self, pointer=pointer, **kwargs)
        self.element_type = element_type
        self.size_is_arg_num = size_is_arg_num
        self.length_is_arg_num = length_is_arg_num
        self.tag = Type.Tags.Array

    @staticmethod
    def read(typelib, map, data_pool, offset, flags):
        """
        Read an ArrayTypeDescriptor at |offset| from the mmaped
        file |map| with data pool offset |data_pool|.
        Returns (ArrayType, next offset),
        where |next offset| is an offset suitable for reading the data
        following this ArrayTypeDescriptor.
        """
        if not flags['pointer']:
            return None, offset
        start = data_pool + offset - 1
        (size_is_arg_num, length_is_arg_num) = ArrayType._descriptor.unpack(map[start:start + ArrayType._descriptor.size])
        offset += ArrayType._descriptor.size
        t, offset = Type.read(typelib, map, data_pool, offset)
        return ArrayType(t, size_is_arg_num, length_is_arg_num, **flags), offset

    def write(self, typelib, file):
        """
        Write an ArrayTypeDescriptor to |file|, which is assumed
        to be seeked to the proper position.

        """
        Type.write(self, typelib, file)
        file.write(ArrayType._descriptor.pack(self.size_is_arg_num,
                                              self.length_is_arg_num))
        self.element_type.write(typelib, file)

    def __str__(self):
        return "%s []" % str(self.element_type)

class StringWithSizeType(Type):
    """
    A type representing a UTF-8 encoded string whose size and length
    are passed as separate arguments to a method. (StringWithSizeTypeDescriptor
    from the typelib specification.)

    """
    _descriptor = struct.Struct(">BB")

    def __init__(self, size_is_arg_num, length_is_arg_num,
                 pointer=True, **kwargs):
        if not pointer:
            raise DataError, "StringWithSizeType is not valid with pointer=False"
        Type.__init__(self, pointer=pointer, **kwargs)
        self.size_is_arg_num = size_is_arg_num
        self.length_is_arg_num = length_is_arg_num
        self.tag = Type.Tags.StringWithSize

    @staticmethod
    def read(typelib, map, data_pool, offset, flags):
        """
        Read an StringWithSizeTypeDescriptor at |offset| from the mmaped
        file |map| with data pool offset |data_pool|.
        Returns (StringWithSizeType, next offset),
        where |next offset| is an offset suitable for reading the data
        following this StringWithSizeTypeDescriptor.
        """
        if not flags['pointer']:
            return None, offset
        start = data_pool + offset - 1
        (size_is_arg_num, length_is_arg_num) = StringWithSizeType._descriptor.unpack(map[start:start + StringWithSizeType._descriptor.size])
        offset += StringWithSizeType._descriptor.size
        return StringWithSizeType(size_is_arg_num, length_is_arg_num, **flags), offset
    
    def write(self, typelib, file):
        """
        Write a StringWithSizeTypeDescriptor to |file|, which is assumed
        to be seeked to the proper position.

        """
        Type.write(self, typelib, file)
        file.write(StringWithSizeType._descriptor.pack(self.size_is_arg_num,
                                                       self.length_is_arg_num))
        
    def __str__(self):
        return "string_s"

class WideStringWithSizeType(Type):
    """
    A type representing a UTF-16 encoded string whose size and length
    are passed as separate arguments to a method.
    (WideStringWithSizeTypeDescriptor from the typelib specification.)

    """    
    _descriptor = struct.Struct(">BB")

    def __init__(self, size_is_arg_num, length_is_arg_num,
                 pointer=True, **kwargs):
        if not pointer:
            raise DataError, "WideStringWithSizeType is not valid with pointer=False"
        Type.__init__(self, pointer=pointer, **kwargs)
        self.size_is_arg_num = size_is_arg_num
        self.length_is_arg_num = length_is_arg_num
        self.tag = Type.Tags.WideStringWithSize

    @staticmethod
    def read(typelib, map, data_pool, offset, flags):
        """
        Read an WideStringWithSizeTypeDescriptor at |offset| from the mmaped
        file |map| with data pool offset |data_pool|.
        Returns (WideStringWithSizeType, next offset),
        where |next offset| is an offset suitable for reading the data
        following this WideStringWithSizeTypeDescriptor.
        """
        if not flags['pointer']:
            return None, offset
        start = data_pool + offset - 1
        (size_is_arg_num, length_is_arg_num) = WideStringWithSizeType._descriptor.unpack(map[start:start + WideStringWithSizeType._descriptor.size])
        offset += WideStringWithSizeType._descriptor.size
        return WideStringWithSizeType(size_is_arg_num, length_is_arg_num, **flags), offset

    def write(self, typelib, file):
        """
        Write a WideStringWithSizeTypeDescriptor to |file|, which is assumed
        to be seeked to the proper position.

        """
        Type.write(self, typelib, file)
        file.write(WideStringWithSizeType._descriptor.pack(self.size_is_arg_num,
                                                           self.length_is_arg_num))

    def __str__(self):
        return "wstring_s"

class Param(object):
    """
    A parameter to a method, or the return value of a method.
    (ParamDescriptor from the typelib specification.)

    """
    _descriptorstart = struct.Struct(">B")

    def __init__(self, type, in_=True, out=False, retval=False,
                 shared=False, dipper=False, optional=False):
        """
        Construct a Param object with the specified |type| and
        flags. Params default to "in".

        """
        self.type = type
        self.in_ = in_
        self.out = out
        self.retval = retval
        self.shared = shared
        self.dipper = dipper
        self.optional = optional

    @staticmethod
    def decodeflags(byte):
        """
        Given |byte|, an unsigned uint8 containing flag bits,
        decode the flag bits as described in
        http://www.mozilla.org/scriptable/typelib_file.html#ParamDescriptor
        and return a dict of flagname: (True|False) suitable
        for passing to Param.__init__ as **kwargs
        """
        return {'in_': bool(byte & 0x80),
                'out': bool(byte & 0x40),
                'retval': bool(byte & 0x20),
                'shared': bool(byte & 0x10),
                'dipper': bool(byte & 0x08),
                #XXX: Not in the spec, see:
                # http://hg.mozilla.org/mozilla-central/annotate/0e0e2516f04e/xpcom/typelib/xpt/public/xpt_struct.h#l456
                'optional': bool(byte & 0x04),
                }

    def encodeflags(self):
        """
        Encode the flags of this Param. Return a byte suitable for
        writing to a typelib file.

        """
        flags = 0
        if self.in_:
            flags |= 0x80
        if self.out:
            flags |= 0x40
        if self.retval:
            flags |= 0x20
        if self.shared:
            flags |= 0x10
        if self.dipper:
            flags |= 0x08
        if self.optional:
            flags |= 0x04
        return flags

    @staticmethod
    def read(typelib, map, data_pool, offset):
        """
        Read a ParamDescriptor at |offset| from the mmaped file |map| with
        data pool offset |data_pool|. Returns (Param, next offset),
        where |next offset| is an offset suitable for reading the data
        following this ParamDescriptor.
        """
        start = data_pool + offset - 1
        (flags,) = Param._descriptorstart.unpack(map[start:start + Param._descriptorstart.size])
        # only the first five bits are flags
        flags &= 0xFC
        flags = Param.decodeflags(flags)
        offset += Param._descriptorstart.size
        t, offset = Type.read(typelib, map, data_pool, offset)
        p = Param(t, **flags)
        return p, offset

    def write(self, typelib, file):
        """
        Write a ParamDescriptor to |file|, which is assumed to be seeked
        to the correct position.

        """
        file.write(Param._descriptorstart.pack(self.encodeflags()))
        self.type.write(typelib, file)

    def prefix(self):
        """
        Return a human-readable string representing the flags set
        on this Param.

        """
        s = ""
        if self.out:
            if self.in_:
                s = "inout "
            else:
                s = "out "
        else:
            s = "in "
        if self.dipper:
            s += "dipper "
        if self.retval:
            s += "retval "
        if self.shared:
            s += "shared "
        if self.optional:
            s += "optional "
        return s
            
    def __str__(self):
        return self.prefix() + str(self.type)

class Method(object):
    """
    A method of an interface, defining its associated parameters
    and return value.
    (MethodDescriptor from the typelib specification.)
    
    """
    _descriptorstart = struct.Struct(">BIB")
    
    def __init__(self, name, result,
                 params=[], getter=False, setter=False, notxpcom=False,
                 constructor=False, hidden=False, optargc=False,
                 implicit_jscontext=False):
        self.name = name
        self._name_offset = 0
        self.getter = getter
        self.setter = setter
        self.notxpcom = notxpcom
        self.constructor = constructor
        self.hidden = hidden
        self.optargc = optargc
        self.implicit_jscontext = implicit_jscontext
        self.params = list(params)
        self.result = result

    def read_params(self, typelib, map, data_pool, offset, num_args):
        """
        Read |num_args| ParamDescriptors representing this Method's arguments
        from the mmaped file |map| with data pool at the offset |data_pool|,
        starting at |offset| into self.params. Returns the offset
        suitable for reading the data following the ParamDescriptor array.
        
        """
        for i in range(num_args):
            p, offset = Param.read(typelib, map, data_pool, offset)
            self.params.append(p)
        return offset

    def read_result(self, typelib, map, data_pool, offset):
        """
        Read a ParamDescriptor representing this Method's return type
        from the mmaped file |map| with data pool at the offset |data_pool|,
        starting at |offset| into self.result. Returns the offset
        suitable for reading the data following the ParamDescriptor.
        
        """
        self.result, offset = Param.read(typelib, map, data_pool, offset)
        return offset

    @staticmethod
    def decodeflags(byte):
        """
        Given |byte|, an unsigned uint8 containing flag bits,
        decode the flag bits as described in
        http://www.mozilla.org/scriptable/typelib_file.html#MethodDescriptor
        and return a dict of flagname: (True|False) suitable
        for passing to Method.__init__ as **kwargs
        
        """
        return {'getter': bool(byte & 0x80),
                'setter': bool(byte & 0x40),
                'notxpcom': bool(byte & 0x20),
                'constructor': bool(byte & 0x10),
                'hidden': bool(byte & 0x08),
                # Not in the spec, see
                # http://hg.mozilla.org/mozilla-central/annotate/0e0e2516f04e/xpcom/typelib/xpt/public/xpt_struct.h#l489
                'optargc': bool(byte & 0x04),
                'implicit_jscontext': bool(byte & 0x02),
                }

    def encodeflags(self):
        """
        Encode the flags of this Method object, return a byte suitable
        for writing to a typelib file.

        """
        flags = 0
        if self.getter:
            flags |= 0x80
        if self.setter:
            flags |= 0x40
        if self.notxpcom:
            flags |= 0x20
        if self.constructor:
            flags |= 0x10
        if self.hidden:
            flags |= 0x08
        if self.optargc:
            flags |= 0x04
        if self.implicit_jscontext:
            flags |= 0x02
        return flags

    @staticmethod
    def read(typelib, map, data_pool, offset):
        """
        Read a MethodDescriptor at |offset| from the mmaped file |map| with
        data pool offset |data_pool|. Returns (Method, next offset),
        where |next offset| is an offset suitable for reading the data
        following this MethodDescriptor.
        
        """
        start = data_pool + offset - 1
        flags, name_offset, num_args = Method._descriptorstart.unpack(map[start:start + Method._descriptorstart.size])
        # only the first seven bits are flags
        flags &= 0xFE
        flags = Method.decodeflags(flags)
        name = Typelib.read_string(map, data_pool, name_offset)
        m = Method(name, None, **flags)
        offset += Method._descriptorstart.size
        offset = m.read_params(typelib, map, data_pool, offset, num_args)
        offset = m.read_result(typelib, map, data_pool, offset)
        return m, offset

    def write(self, typelib, file):
        """
        Write a MethodDescriptor to |file|, which is assumed to be
        seeked to the right position.

        """
        file.write(Method._descriptorstart.pack(self.encodeflags(),
                                                self._name_offset,
                                                len(self.params)))
        for p in self.params:
            p.write(typelib, file)
        self.result.write(typelib, file)

    def write_name(self, file, data_pool_offset):
        """
        Write this method's name to |file|.
        Assumes that |file| is currently seeked to an unused portion
        of the data pool.

        """
        if self.name:
            self._name_offset = file.tell() - data_pool_offset + 1
            file.write(self.name + "\x00")
        else:
            self._name_offset = 0

class Constant(object):
    """
    A constant value of a specific type defined on an interface.
    (ConstantDesciptor from the typelib specification.)

    """
    _descriptorstart = struct.Struct(">I")
    # Actual value is restricted to this set of types
    #XXX: the spec lies, the source allows a bunch more
    # http://hg.mozilla.org/mozilla-central/annotate/9c85f9aaec8c/xpcom/typelib/xpt/src/xpt_struct.c#l689
    typemap = {Type.Tags.int16: '>h',
               Type.Tags.uint16: '>H',
               Type.Tags.int32: '>i',
               Type.Tags.uint32: '>I'}

    def __init__(self, name, type, value):
        self.name = name
        self._name_offset = 0        
        self.type = type
        self.value = value

    @staticmethod
    def read(typelib, map, data_pool, offset):
        """
        Read a ConstDescriptor at |offset| from the mmaped file |map| with
        data pool offset |data_pool|. Returns (Constant, next offset),
        where |next offset| is an offset suitable for reading the data
        following this ConstDescriptor.
        
        """
        start = data_pool + offset - 1
        (name_offset,) = Constant._descriptorstart.unpack(map[start:start + Constant._descriptorstart.size])
        name = Typelib.read_string(map, data_pool, name_offset)
        offset += Constant._descriptorstart.size
        # Read TypeDescriptor
        t, offset = Type.read(typelib, map, data_pool, offset)
        c = None
        if isinstance(t, SimpleType) and t.tag in Constant.typemap:
            tt = Constant.typemap[t.tag]
            start = data_pool + offset - 1
            (val,) = struct.unpack(tt, map[start:start + struct.calcsize(tt)])
            offset += struct.calcsize(tt)
            c = Constant(name, t, val)
        return c, offset

    def write(self, typelib, file):
        """
        Write a ConstDescriptor to |file|, which is assumed
        to be seeked to the proper position.

        """
        file.write(Constant._descriptorstart.pack(self._name_offset))
        self.type.write(typelib, file)
        tt = Constant.typemap[self.type.tag]
        file.write(struct.pack(tt, self.value))

    def write_name(self, file, data_pool_offset):
        """
        Write this constants's name to |file|.
        Assumes that |file| is currently seeked to an unused portion
        of the data pool.

        """
        if self.name:
            self._name_offset = file.tell() - data_pool_offset + 1
            file.write(self.name + "\x00")
        else:
            self._name_offset = 0

    def __repr__(self):
        return "Constant(%s, %s, %d)" % (self.name, str(self.type), self.value)

class Interface(object):
    """
    An Interface represents an object, with its associated methods
    and constant values.
    (InterfaceDescriptor from the typelib specification.)
    
    """
    _direntry = struct.Struct(">16sIII")    
    _descriptorstart = struct.Struct(">HH")

    UNRESOLVED_IID = "00000000-0000-0000-0000-000000000000"
    
    def __init__(self, name, iid=UNRESOLVED_IID, namespace="",
                 resolved=False, parent=None, methods=[], constants=[],
                 scriptable=False, function=False):
        self.resolved = resolved
        #TODO: should validate IIDs!
        self.iid = iid
        self.name = name
        self.namespace = namespace
        # if unresolved, all the members following this are unusable
        self.parent = parent
        self.methods = list(methods)
        self.constants = list(constants)
        self.scriptable = scriptable
        self.function = function
        # For sanity, if someone constructs an Interface and passes
        # in methods or constants, then it's resolved.
        if self.methods or self.constants:
            # make sure it has a valid IID
            if self.iid == Interface.UNRESOLVED_IID:
                raise DataError, "Cannot instantiate Interface %s containing methods or constants with an unresolved IID" % self.name
            self.resolved = True
        # These are only used for writing out the interface
        self._descriptor_offset = 0
        self._name_offset = 0
        self._namespace_offset = 0

    def __repr__(self):
        return "Interface('%s', '%s', '%s', methods=%s)" % (self.name, self.iid, self.namespace, self.methods)

    def __str__(self):
        return "Interface(name='%s', iid='%s')" % (self.name, self.iid)

    def __cmp__(self, other):
        c = cmp(self.iid, other.iid)
        if c != 0:
            return c
        c = cmp(self.name, other.name)
        if c != 0:
            return c
        c = cmp(self.namespace, other.namespace)
        if c != 0:
            return c
        # names and IIDs are the same, check resolved
        if self.resolved != other.resolved:
            if self.resolved:
                return -1
            else:
                return 1
        else:
            # both unresolved, but names and IIDs are the same, so equal
            return 0
        #TODO: actually compare methods etc
        return 0

    def read_descriptor(self, typelib, map, data_pool):
        offset = self._descriptor_offset
        if offset == 0:
            return
        start = data_pool + offset - 1
        parent, num_methods = Interface._descriptorstart.unpack(map[start:start + Interface._descriptorstart.size])
        if parent > 0 and parent <= len(typelib.interfaces):
            self.parent = typelib.interfaces[parent - 1]
        # Read methods
        offset += Interface._descriptorstart.size
        for i in range(num_methods):
            m, offset = Method.read(typelib, map, data_pool, offset)
            self.methods.append(m)
        # Read constants
        start = data_pool + offset - 1
        (num_constants, ) = struct.unpack(">H", map[start:start + struct.calcsize(">H")])
        offset = offset + struct.calcsize(">H")
        for i in range(num_constants):
            c, offset = Constant.read(typelib, map, data_pool, offset)
            self.constants.append(c)
        # Read flags
        start = data_pool + offset - 1
        (flags, ) = struct.unpack(">B", map[start:start + struct.calcsize(">B")])
        offset = offset + struct.calcsize(">B")
        # only the first two bits are flags
        flags &= 0xC0
        if flags & 0x80:
            self.scriptable = True
        if flags & 0x40:
            self.function = True
        self.resolved = True

    def write_directory_entry(self, file):
        """
        Write an InterfaceDirectoryEntry for this interface
        to |file|, which is assumed to be seeked to the correct offset.

        """
        file.write(Interface._direntry.pack(Typelib.string_to_iid(self.iid),
                                            self._name_offset,
                                            self._namespace_offset,
                                            self._descriptor_offset))

    def write(self, typelib, file, data_pool_offset):
        """
        Write an InterfaceDescriptor to |file|, which is assumed
        to be seeked to the proper position. If this interface
        is not resolved, do not write any data.

        """
        if not self.resolved:
            self._descriptor_offset = 0
            return
        self._descriptor_offset = file.tell() - data_pool_offset + 1
        parent_idx = 0
        if self.parent:
            parent_idx = typelib.interfaces.index(self.parent) + 1
        file.write(Interface._descriptorstart.pack(parent_idx, len(self.methods)))
        for m in self.methods:
            m.write(typelib, file)
        file.write(struct.pack(">H", len(self.constants)))
        for c in self.constants:
            c.write(typelib, file)
        flags = 0
        if self.scriptable:
            flags |= 0x80
        if self.function:
            flags |= 0x40
        file.write(struct.pack(">B", flags))
        
    def write_names(self, file, data_pool_offset):
        """
        Write this interface's name and namespace to |file|,
        as well as the names of all of its methods and constants.
        Assumes that |file| is currently seeked to an unused portion
        of the data pool.

        """
        if self.name:
            self._name_offset = file.tell() - data_pool_offset + 1
            file.write(self.name + "\x00")
        else:
            self._name_offset = 0
        if self.namespace:
            self._namespace_offset = file.tell() - data_pool_offset + 1
            file.write(self.namespace + "\x00")
        else:
            self._namespace_offset = 0
        for m in self.methods:
            m.write_name(file, data_pool_offset)
        for c in self.constants:
            c.write_name(file, data_pool_offset)

class Typelib(object):
    """
    A typelib represents one entire typelib file and all the interfaces
    referenced within, whether defined entirely within the typelib or
    merely referenced by name or IID.

    Typelib objects may be instantiated directly and populated with data,
    or the static Typelib.read method may be called to read one from a file.

    """
    _header = struct.Struct(">16sBBHIII")

    def __init__(self, version=TYPELIB_VERSION, interfaces=[], annotations=[]):
        """
        Instantiate a new Typelib.

        """
        self.version = version
        self.interfaces = list(interfaces)
        self.annotations = list(annotations)

    @staticmethod
    def iid_to_string(iid):
        """
        Convert a 16-byte IID into a UUID string.

        """
        def hexify(s):
            return ''.join(["%02x" % ord(x) for x in s])
        return "%s-%s-%s-%s-%s" % (hexify(iid[:4]), hexify(iid[4:6]),
                                   hexify(iid[6:8]), hexify(iid[8:10]),
                                   hexify(iid[10:]))

    @staticmethod
    def string_to_iid(iid_str):
        """
        Convert a UUID string into a 16-byte IID.

        """
        s = iid_str.replace('-','')
        return ''.join([chr(int(s[i:i+2], 16)) for i in range(0, len(s), 2)])
    
    @staticmethod
    def read_string(map, data_pool, offset):
        if offset == 0:
            return ""
        sz = map.find('\x00', data_pool + offset - 1)
        if sz == -1:
            return ""
        return map[data_pool + offset - 1:sz]
    
    @staticmethod
    def read(filename):
        """
        Read a typelib from the file named |filename| and return
        the constructed Typelib object.

        """
        with open(filename, "r+b") as f:
            st = os.fstat(f.fileno())
            map = f.read(st.st_size)
            data = Typelib._header.unpack(map[:Typelib._header.size])
            if data[0] != XPT_MAGIC:
                raise FileFormatError, "Bad magic: %s" % data[0]
            xpt = Typelib((data[1], data[2]))
            num_interfaces = data[3]
            file_length = data[4]
            if file_length != st.st_size:
                raise FileFormatError, "File is of wrong length, got %d bytes, expected %d" % (st.st_size, file_length)
            #XXX: by spec this is a zero-based file offset. however,
            # the xpt_xdr code always subtracts 1 from data offsets
            # (because that's what you do in the data pool) so it
            # winds up accidentally treating this as 1-based.
            # Filed as: https://bugzilla.mozilla.org/show_bug.cgi?id=575343
            interface_directory_offset = data[5] - 1
            data_pool_offset = data[6]
            # make a half-hearted attempt to read Annotations,
            # since XPIDL doesn't produce any anyway.
            start = Typelib._header.size
            (anno, ) = struct.unpack(">B", map[start:start + struct.calcsize(">B")])
            islast = anno & 0x80
            tag = anno & 0x7F
            if tag == 0: # EmptyAnnotation
                xpt.annotations.append(None)
            # We don't bother handling PrivateAnnotations or anything
            
            for i in range(num_interfaces):
                # iid, name, namespace, interface_descriptor
                start = interface_directory_offset + i * Interface._direntry.size
                end = interface_directory_offset + (i+1) * Interface._direntry.size
                ide = Interface._direntry.unpack(map[start:end])
                iid = Typelib.iid_to_string(ide[0])
                name = Typelib.read_string(map, data_pool_offset, ide[1])
                namespace = Typelib.read_string(map, data_pool_offset, ide[2])
                iface = Interface(name, iid, namespace)
                iface._descriptor_offset = ide[3]
                xpt.interfaces.append(iface)
            for iface in xpt.interfaces:
                iface.read_descriptor(xpt, map, data_pool_offset)
        return xpt
    
    def __repr__(self):
        return "<Typelib with %d interfaces>" % len(self.interfaces)

    def _sanityCheck(self):
        """
        Check certain assumptions about data contained in this typelib.
        Sort the interfaces array by IID, check that all interfaces
        referenced by methods exist in the array.

        """
        self.interfaces.sort()
        for i in self.interfaces:
            if i.parent and i.parent not in self.interfaces:
                raise DataError, "Interface %s has parent %s not present in typelib!" % (i.name, i.parent.name)
            for m in i.methods:
                for n, p in enumerate(m.params):
                    if isinstance(p, InterfaceType) and \
                        p.iface not in self.interfaces:
                        raise DataError, "Interface method %s::%s, parameter %d references interface %s not present in typelib!" % (i.name, m.name, n, p.iface.name)
                if isinstance(m.result, InterfaceType) and m.result.iface not in self.interfaces:
                    raise DataError, "Interface method %s::%s, result references interface %s not present in typelib!" % (i.name, m.name, m.result.iface.name)

    def write(self, filename):
        """
        Write the contents of this typelib to the file named |filename|.

        """
        self._sanityCheck()
        with open(filename, "wb") as f:
            # write out space for a header + one empty annotation,
            # padded to 4-byte alignment.
            headersize = (Typelib._header.size + 1)
            if headersize % 4:
                headersize += 4 - headersize % 4
            f.write("\x00" * headersize)
            # save this offset, it's the interface directory offset.
            interface_directory_offset = f.tell()
            # write out space for an interface directory
            f.write("\x00" * Interface._direntry.size * len(self.interfaces))
            # save this offset, it's the data pool offset.
            data_pool_offset = f.tell()
            # write out all the interface descriptors to the data pool
            for i in self.interfaces:
                i.write_names(f, data_pool_offset)
                i.write(self, f, data_pool_offset)
            # now, seek back and write the header
            file_len = f.tell()
            f.seek(0)
            f.write(Typelib._header.pack(XPT_MAGIC,
                                         TYPELIB_VERSION[0],
                                         TYPELIB_VERSION[1],
                                         len(self.interfaces),
                                         file_len,
                                         interface_directory_offset,
                                         data_pool_offset))
            # write an empty annotation
            f.write(struct.pack(">B", 0x80))
            # now write the interface directory
            #XXX: bug-compatible with existing xpt lib, put it one byte
            # ahead of where it's supposed to be.
            f.seek(interface_directory_offset - 1)
            for i in self.interfaces:
                i.write_directory_entry(f)

    def merge(self, other):
        """
        Merge the contents of Typelib |other| into this typelib.

        """
        # This will be a list of (replaced interface, replaced with)
        # containing interfaces that were replaced with interfaces from
        # another typelib, and the interface that replaced them.
        merged_interfaces = []
        for i in other.interfaces:
            if i in self.interfaces:
                continue
            # See if there's a copy of this interface with different
            # resolved status or IID value.
            merged = False
            for j in self.interfaces:
                if i.name == j.name:
                    if i.resolved != j.resolved:
                        # prefer resolved interfaces over unresolved
                        if j.resolved:
                            # keep j
                            merged_interfaces.append((i, j))
                            merged = True
                            # Fixup will happen after processing all interfaces.
                        else:
                            # replace j with i
                            merged_interfaces.append((j, i))
                            merged = True
                            self.interfaces[self.interfaces.index(j)] = i
                    elif i.iid != j.iid:
                        # Prefer unresolved interfaces with valid IIDs
                        if j.iid == Interface.UNRESOLVED_IID:
                            # replace j with i
                            merged_interfaces.append((j, i))
                            merged = True
                            self.interfaces[self.interfaces.index(j)] = i
                        elif i.iid == Interface.UNRESOLVED_IID:
                            # keep j
                            merged_interfaces.append((i, j))
                            merged = True
                            # Fixup will happen after processing all interfaces.
                        else:
                            # Same name, different IIDs, raise an exception
                            raise DataError, \
                                  "Typelibs contain definitions of interface %s"\
                                  " with different IIDs!" % i.name
                elif i.iid == j.iid and i.iid != Interface.UNRESOLVED_IID:
                    # Same IID, different names, raise an exception
                    raise DataError, \
                          "Typelibs contain definitions of interface %s"\
                          " with different names (%s vs. %s)!" %  \
                          (i.iid, i.name, j.name)
            if not merged:
                # No partially matching interfaces, so just take this interface
                self.interfaces.append(i)
        # Now fixup any merged interfaces
        def checkType(t, replaced_from, replaced_to):
            if isinstance(t, InterfaceType) and t.iface == replaced_from:
                t.iface = replaced_to
            elif isinstance(t, ArrayType) and \
                 isinstance(t.element_type, InterfaceType) and \
                 t.element_type.iface == replaced_from:
                t.element_type.iface = replaced_to
        
        for replaced_from, replaced_to in merged_interfaces:
            for i in self.interfaces:
                # Replace parent references
                if i.parent is not None and i.parent == replaced_from:
                    i.parent = replaced_to
                for m in i.methods:
                    # Replace InterfaceType params and return values
                    checkType(m.result.type, replaced_from, replaced_to)
                    for p in m.params:
                        checkType(p.type, replaced_from, replaced_to)
        #TODO: do we care about annotations? probably not

    def dump(self, out):
        """
        Print a human-readable listing of the contents of this typelib
        to |out|, in the format of xpt_dump.
        
        """
        out.write("""Header:
   Major version:         %d
   Minor version:         %d
   Number of interfaces:  %d
   Annotations:\n""" % (self.version[0], self.version[1], len(self.interfaces)))
        for i, a in enumerate(self.annotations):
            if a is None:
                out.write("      Annotation #%d is empty.\n" % i)
        out.write("\nInterface Directory:\n")
        for i in self.interfaces:
            out.write("   - %s::%s (%s):\n" % (i.namespace, i.name, i.iid))
            if not i.resolved:
                out.write("      [Unresolved]\n")
            else:
                if i.parent:
                    out.write("      Parent: %s::%s\n" % (i.parent.namespace,
                                                    i.parent.name))
                out.write("""      Flags:
         Scriptable: %s
         Function: %s\n""" % (i.scriptable and "TRUE" or "FALSE",
                              i.function and "TRUE" or "FALSE"))
                out.write("      Methods:\n")
                if len(i.methods) == 0:
                    out.write("         No Methods\n")
                else:
                    for m in i.methods:
                        out.write("   %s%s%s%s%s%s%s %s %s(%s);\n" % (
                            m.getter and "G" or " ",
                            m.setter and "S" or " ",
                            m.hidden and "H" or " ",
                            m.notxpcom and "N" or " ",
                            m.constructor and "C" or " ",
                            m.optargc and "O" or " ",
                            m.implicit_jscontext and "J" or " ",
                            str(m.result.type),
                            m.name,
                            m.params and ", ".join(str(p) for p in m.params) or ""
                            ))
                out.write("      Constants:\n")
                if len(i.constants) == 0:
                    out.write("         No Constants\n")
                else:
                    for c in i.constants:
                        out.write("         %s %s = %d;\n" % (c.type, c.name, c.value))

def xpt_dump(file):
    """
    Dump the contents of |file| to stdout in the format of xpt_dump.

    """
    t = Typelib.read(file)
    t.dump(sys.stdout)

def xpt_link(dest, inputs):
    """
    Link all of the xpt files in |inputs| together and write the
    result ot |dest|.

    """
    if not inputs:
        print >>sys.stderr, "Usage: xpt_link <destination file> <input files>"
        return
    t1 = Typelib.read(inputs[0])
    for f in inputs[1:]:
        t2 = Typelib.read(f)
        t1.merge(t2)
    t1.write(dest)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print >>sys.stderr, "xpt <dump|link> <files>"
        sys.exit(1)
    if sys.argv[1] == 'dump':
        xpt_dump(sys.argv[2])
    elif sys.argv[1] == 'link':
        xpt_link(sys.argv[2], sys.argv[3:])
