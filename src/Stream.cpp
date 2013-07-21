// This file is part of libZelda.
//
// libZelda is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// libZelda is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with libZelda.  If not, see <http://www.gnu.org/licenses/>

#include "Stream.hpp"
#include "IOException.hpp"
#include "InvalidOperationException.hpp"
#include <string.h>
#include <sstream>

namespace zelda
{
namespace io
{

const Uint32 Stream::BLOCKSZ = 512;

Stream::Stream() :
    m_bitPosition(0),
    m_position(0),
    m_length(0),
    m_endian(LittleEndian),
    m_data(NULL),
    m_autoResize(true)
{}

Stream::Stream(const Uint8* data, Uint64 length) :
    m_bitPosition(0),
    m_position(0),
    m_endian(LittleEndian),
    m_autoResize(true)
{
    if (length <= 0)
        throw InvalidOperationException("Length cannot be <= to 0");

    m_length = length;
    if (data)
        m_data = (Uint8*)data;
    else
    {
        m_data = new Uint8[m_length];
        memset(m_data, 0, m_length);
    }
}

Stream::Stream(Int64 length) :
    m_bitPosition(0),
    m_position(0),
    m_length(length)
{
    m_data = new Uint8[m_length];
    memset(m_data, 0, m_length);
}

Stream::Stream(Stream* stream)
{
    if (m_data)
        delete[] m_data;

    m_data = stream->m_data;
    m_position = stream->m_position;
    m_length = stream->m_length;
}

Stream::~Stream()
{
    if (m_data)
        delete[] m_data;

    m_data = NULL;
    m_position = 0;
    m_length = 0;
    m_endian = LittleEndian;
    m_autoResize = false;
}

void Stream::writeBit(bool val)
{
    if (m_position + sizeof(Uint8) > m_length && m_autoResize)
        resize(m_position + sizeof(Uint8));
    else if (m_position > m_length)
        throw IOException("Stream::writeBit() -> Position outside stream bounds");

    *(Uint8*)(m_data + m_position) |= ((Uint32)val << m_bitPosition);
    m_bitPosition++;
    if (m_bitPosition > 7)
    {
        m_bitPosition = 0;
        m_position += sizeof(Uint8);
    }
}

void Stream::writeUByte(Uint8 byte)
{
    writeByte((Int8)byte);
}

void Stream::writeByte(Int8 byte)
{
    if (m_bitPosition > 0)
    {
        m_bitPosition = 0;
        m_position += sizeof(Uint8);
    }
    if (m_position + 1 > m_length && m_autoResize)
        resize(m_position + 1);
    else if (m_position > m_length)
        throw IOException("Stream::writeByte() -> Position outside stream bounds");

    *(Int8*)(m_data + m_position) = byte;
    m_position++;
}

void Stream::writeUBytes(Uint8* data, Int64 length)
{
    writeBytes((Int8*)data, length);
}

void Stream::writeBytes(Int8* data, Int64 length)
{
    if (m_bitPosition > 0)
    {
        m_bitPosition = 0;
        m_position += sizeof(Uint8);
    }

    if (!data)
        throw InvalidOperationException("BinaryWriter::writeBytes() -> data cannnot be NULL");
    if (m_position + length > m_length && m_autoResize)
        resize(m_position + length);
    else if (m_position > m_length)
        throw IOException("BinaryWriter::writeBytes() -> Position outside stream bounds");


    memcpy((Int8*)(m_data + m_position), data, length);

    m_position += length;
}

bool Stream::readBit()
{
    if (m_position + sizeof(Uint8) > m_length && m_autoResize)
        resize(m_position + sizeof(Uint8));
    else if (m_position > m_length)
        throw IOException("BinaryWriter::WriteInt16() -> Position outside stream bounds");

    bool ret = (*(Uint8*)(m_data + m_position) & (1 << m_bitPosition));

    m_bitPosition++;
    if (m_bitPosition > 7)
    {
        m_bitPosition = 0;
        m_position += sizeof(Uint8);
    }

    return ret;
}

Int8 Stream::readByte()
{
    if (m_bitPosition > 0)
    {
        m_bitPosition = 0;
        m_position += sizeof(Uint8);
    }
    if (m_position + 1 > m_length)
        throw IOException("Position passed stream bounds");

    return *(Int8*)(m_data + m_position++);
}

Int8* Stream::readBytes(Int64 length)
{
    if (m_bitPosition > 0)
    {
        m_bitPosition = 0;
        m_position += sizeof(Uint8);
    }

    if (m_position + length > m_length)
        throw IOException("Position passed stream bounds: " + m_position);

    Int8* ret = new Int8[length];
    memcpy(ret, (const Int8*)(m_data + m_position), length);
    m_position += length;
    return ret;
}

void Stream::seek(Int64 position, SeekOrigin origin)
{
    switch (origin)
    {
        case Beginning:
            if ((position < 0 || (Int64)position > (Int64)m_length) && !m_autoResize)
            {
                std::stringstream ss;
                ss << position;
                throw IOException("Stream::seek() Beginnning -> Position outside stream bounds: " + ss.str());
            }
            if ((Uint64)position > m_length)
                this->resize(position);
            m_position = position;
            break;
        case Current:
            if ((((Int64)m_position + position) < 0 || (m_position + position) > m_length) && !m_autoResize)
            {
                std::stringstream ss;
                ss << (m_position + position);
                throw IOException("Stream::seek() Current -> Position outside stream bounds: " + ss.str());
            }
            else if ((m_position + position) > m_length)
                this->resize(m_position + position);

            m_position += position;
            break;
        case End:
            if ((((Int64)m_length - position < 0) || (m_length - position) > m_length) && !m_autoResize)
            {
                std::stringstream ss;
                ss << std::hex << "0x" << (m_length - position);
                throw IOException("Stream::seek() End -> Position outside stream bounds " + ss.str());
            }
            else if ((m_length - position) > m_length)
                this->resize(m_length - position);

            m_position = m_length - position;
            break;
    }
}

void Stream::resize(Uint64 newSize)
{
    if (newSize < m_length)
        throw InvalidOperationException("Stream::Resize() -> New size cannot be less to the old size.");

    // Allocate and copy new buffer
    Uint8* newArray = new Uint8[newSize];
    memset(newArray, 0, newSize);

    memcpy(newArray, m_data, m_length);

    // Delete the old one
    delete[] m_data;

    // Swap the pointer and size out for the new ones.
    m_data = newArray;
    m_length = newSize;
}

void  Stream::setData(const Uint8* data, Uint64 length)
{
    if (m_data)
        delete[] m_data;

    m_data = (Uint8*)data;
    m_length = length;
    m_position = 0;
}

Uint8* Stream::data() const
{
    Uint8* ret = new Uint8[m_length];
    memset(ret, 0, m_length);
    memcpy(ret, m_data, m_length);
    return ret;
}

Int64 Stream::length()
{
    return m_length;
}

Int64 Stream::position()
{
    return m_position;
}

bool Stream::atEnd()
{
    return m_position >= m_length;
}

void Stream::setAutoResizing(bool val)
{
    m_autoResize = val;
}

bool Stream::autoResizing() const
{
    return m_autoResize;
}

bool Stream::isOpenForReading() const
{
    return true;
}

bool Stream::isOpenForWriting() const
{
    return true;
}

void Stream::setEndianess(Endian endian)
{
    m_endian = endian;
}

Endian Stream::endian() const
{
    return m_endian;
}

} // io
} // zelda
