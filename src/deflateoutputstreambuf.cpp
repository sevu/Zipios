/*
  Zipios++ - a small C++ library that provides easy access to .zip files.

  Copyright (C) 2000-2007  Thomas Sondergaard
  Copyright (C) 2015  Made to Order Software Corporation

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

/** \file
 * \brief Implementation of zipios::DeflateOutputStreambuf.
 *
 * This is the counterpart of the zipios::InflateInputStreambuf.
 */

#include "deflateoutputstreambuf.hpp"

#include "zipios++/zipiosexceptions.hpp"

#include "zipios_common.hpp"


namespace zipios
{

/** \class DeflateOutputStreambuf
 * \brief A class to handle stream deflate on the fly.
 *
 * DeflateOutputStreambuf is an output stream filter, that deflates
 * the data that is written to it before it passes it on to the
 * output stream it is attached to. Deflation/Inflation is a
 * compression/decompression method used in gzip and zip. The zlib
 * library is used to perform the actual deflation, this class only
 * wraps the functionality in an output stream filter.
 */


/** \brief Initialize a DeflateOutputStreambuf object.
 *
 * This function initializes the DeflateOutputStreambuf object to make it
 * ready for compressing data using the zlib library.
 *
 * \param[in,out] outbuf  The streambuf to use for output.
 */
DeflateOutputStreambuf::DeflateOutputStreambuf(std::streambuf *outbuf)
    : FilterOutputStreambuf(outbuf)
    //, m_zs() -- auto-init
    //, m_zs_initialized(false) -- auto-init
    , m_invec(getBufferSize())
    , m_outvec(getBufferSize())
    //, m_crc32(0) -- auto-init
    //, m_overflown_bytes(0) -- auto-init
{
    // NOTICE: It is important that this constructor and the methods it
    //         calls does not do anything with the output streambuf m_outbuf.
    //         The reason is that this class can be subclassed, and the
    //         subclass should get a chance to write to the buffer first.

    // zlib init:
    m_zs.zalloc = Z_NULL;
    m_zs.zfree  = Z_NULL;
    m_zs.opaque = Z_NULL;
}


/** \brief Clean up any resources used by this object.
 *
 * The destructor makes sure that the zlib library is done with all
 * the input and output data by calling various flush functions. It
 * then makes sure that the remaining data from zlib is printed in
 * the output file.
 *
 * This is similar to calling closeStream() explicitly.
 */
DeflateOutputStreambuf::~DeflateOutputStreambuf()
{
    closeStream();
}


/** \brief Initialize the zlib library.
 *
 * This method is called in the constructor, so it must not write
 * anything to the output streambuf m_outbuf (see notice in
 * constructor.)
 *
 * It will initialize the output stream as required to accept data
 * to be compressed using the zlib library. The compression level
 * is expected to come from the FileEntry which is about to be
 * saved in the file.
 *
 * \return true if the initialization succeeded, false otherwise.
 */
bool DeflateOutputStreambuf::init(FileEntry::CompressionLevel compression_level)
{
    if(m_zs_initialized)
    {
        throw std::logic_error("DeflateOutputStreambuf::init(): initialization function called when the class is already initialized. This is not supported.");
    }
    m_zs_initialized = true;
    m_bytes_to_skip = 0;

    int const default_mem_level(8);

    int zlevel(Z_NO_COMPRESSION);
    switch(compression_level)
    {
    case FileEntry::COMPRESSION_LEVEL_DEFAULT:
        zlevel = Z_DEFAULT_COMPRESSION;
        break;

    case FileEntry::COMPRESSION_LEVEL_SMALLEST:
        zlevel = Z_BEST_COMPRESSION;
        break;

    case FileEntry::COMPRESSION_LEVEL_FASTEST:
        zlevel = Z_BEST_SPEED;
        break;

    case FileEntry::COMPRESSION_LEVEL_NONE:
        zlevel = Z_NO_COMPRESSION;
        m_bytes_to_skip = 5; // zlib adds 5 bytes in a header we do not want in the output
        break;

    default:
        if(compression_level < FileEntry::COMPRESSION_LEVEL_MINIMUM
        || compression_level > FileEntry::COMPRESSION_LEVEL_MAXIMUM)
        {
            throw std::logic_error("the compression level must be defined between -3 and 100, see the zipios++/fileentry.hpp for a list of valid levels.");
        }
        // The zlevel is calculated linearly from the user specified value
        // of 1 to 100
        //
        // The calculation goes as follow:
        //
        //    x = user specified value - 1    (0 to 99)
        //    x = x * 8                       (0 to 792)
        //    x = x + 11 / 2                  (5 to 797, i.e. +5 with integers)
        //    x = x / 99                      (0 to 8)
        //    x = x + 1                       (1 to 9)
        //
        zlevel = ((compression_level - 1) * 8 + 11 / 2) / 99 + 1;
        break;

    }

    // m_zs.next_in and avail_in must be set according to
    // zlib.h (inline doc).
    m_zs.next_in  = reinterpret_cast<unsigned char *>(&m_invec[0]);
    m_zs.avail_in = 0;

    m_zs.next_out  = reinterpret_cast<unsigned char *>(&m_outvec[0]);
    m_zs.avail_out = getBufferSize();

    //
    // windowBits is passed -MAX_WBITS to tell that no zlib
    // header should be written.
    //
    int const err = deflateInit2(&m_zs, zlevel, Z_DEFLATED, -MAX_WBITS, default_mem_level, Z_DEFAULT_STRATEGY);
    if(err != Z_OK)
    {
        std::ostringstream msgs;
        msgs << "DeflateOutputStreambuf::init(): error while initializing zlib, " << zError(err) << std::endl;
        throw IOException(msgs.str());
    }

    // streambuf init:
    setp(&m_invec[0], &m_invec[0] + getBufferSize());

    m_crc32 = crc32(0, Z_NULL, 0);
    m_overflown_bytes = 0;

    return err == Z_OK;
}


/** \brief Closing the stream.
 *
 * This function is expected to be called once the stream is getting
 * closed (the buffer is destroyed.)
 *
 * It ensures that the zlib library last few bytes get flushed and
 * then mark the class as closed.
 *
 * Note that this function can be called to close the current zlib
 * library stream and start a new one. It is actually called from
 * the putNextEntry() function (via the closeEntry() function.)
 */
void DeflateOutputStreambuf::closeStream()
{
    if(m_zs_initialized)
    {
        m_zs_initialized = false;

        // flush any remaining data
        endDeflation();

        int const err(deflateEnd(&m_zs));
        if(err != Z_OK
        && (err != Z_DATA_ERROR || m_overflown_bytes > 0)) // when we close a directory, we get the Z_DATA_ERROR!
        {
            std::ostringstream msgs;
            msgs << "DeflateOutputStreambuf::closeStream(): deflateEnd failed: " << zError(err) << std::endl;
            throw IOException(msgs.str());
        }
    }
}


/** \brief Get the CRC32 of the file.
 *
 * This function returns the CRC32 for the current file.
 *
 * The returned value is the CRC for the data that has been compressed
 * already (due to calls to overflow()). As DeflateOutputStreambuf may
 * buffer an arbitrary amount of bytes until closeStream() has been
 * invoked, the returned value is not very useful before closeStream()
 * has been called.
 *
 * \return The CRC32 of the last file that was passed through.
 */
uint32_t DeflateOutputStreambuf::getCrc32() const
{
    return m_crc32;
}


/** \brief Retrieve the size of the file deflated.
 *
 * This function returns the number of bytes written to the
 * streambuf object and that were processed from the input
 * buffer by the compressor. After closeStream() has been
 * called this number is the total number of bytes written
 * to the stream. In other words, the size of the uncompressed
 * data.
 *
 * \return The uncompressed size of the file that got written here.
 */
size_t DeflateOutputStreambuf::getSize() const
{
    return m_overflown_bytes;
}


/** \brief Handle an overflow.
 *
 * This function is called by the streambuf implementation whenever
 * "too many bytes" are in the output buffer, ready to be compressed.
 *
 * \exception IOException
 * This exception is raised whenever the overflow() function calls
 * a zlib library function which returns an error.
 *
 * \param[in] c  The character (byte) that overflowed the buffer.
 *
 * \return Always zero (0).
 */
int DeflateOutputStreambuf::overflow(int c)
{
    m_zs.avail_in = pptr() - pbase();
    m_zs.next_in = reinterpret_cast<unsigned char *>(&m_invec[0]);

    m_crc32 = crc32(m_crc32, m_zs.next_in, m_zs.avail_in); // update crc32
    m_overflown_bytes += m_zs.avail_in;

    m_zs.next_out  = reinterpret_cast<unsigned char *>(&m_outvec[0]);
    m_zs.avail_out = getBufferSize();

    // Deflate until _invec is empty.
    int err(Z_OK);
    while((m_zs.avail_in > 0 || m_zs.avail_out == 0) && err == Z_OK)
    {
        if(m_zs.avail_out == 0)
        {
            flushOutvec();
        }

        err = deflate(&m_zs, Z_NO_FLUSH);
    }

    flushOutvec();

    // Update 'put' pointers
    setp(&m_invec[0], &m_invec[0] + getBufferSize());

    if(err != Z_OK && err != Z_STREAM_END)
    {
        // Throw an exception to make istream set badbit
        OutputStringStream msgs;
        msgs << "Deflation failed:" << zError(err);
        throw IOException(msgs.str());
    }

    if(c != EOF)
    {
        *pptr() = c;
        pbump(1);
    }

    return 0;
}


/** \brief Synchronize the buffer.
 *
 * At this time this function does nothing. Do we need to implement
 * something?
 */
int DeflateOutputStreambuf::sync()
{
    /** \FIXME
     * Do something in the sync() function?
     */
    //return overflow();
    return 0;
}


/** \brief Flush the cached output data.
 *
 * This function flushes m_outvec and updates the output pointer
 * and size m_zs.next_out and m_zs.avail_out.
 */
void DeflateOutputStreambuf::flushOutvec()
{
    /** \TODO
     * We need to redesign the class to allow for STORED files to
     * flow through without the need have this crap of bytes to
     * skip...
     */
    size_t deflated_bytes(getBufferSize() - m_zs.avail_out);
    size_t offset(0);
    if(m_bytes_to_skip > 0)
    {
        if(deflated_bytes > m_bytes_to_skip)
        {
            offset += m_bytes_to_skip;
            deflated_bytes -= m_bytes_to_skip;
            m_bytes_to_skip = 0;
        }
        else
        {
            m_bytes_to_skip -= deflated_bytes;
            deflated_bytes = 0;
        }
    }
    if(deflated_bytes > 0)
    {
        size_t const bc(m_outbuf->sputn(&m_outvec[0] + offset, deflated_bytes));
        if(deflated_bytes != bc)
        {
            throw IOException("DeflateOutputStreambuf::flushOutvec(): zlib generated an error.");
        }
    }

    m_zs.next_out = reinterpret_cast<unsigned char *>(&m_outvec[0]);
    m_zs.avail_out = getBufferSize();
}


/** Flushes the remaining data in the zlib buffers, after which the
  only possible operations are deflateEnd() or deflateReset(). */
void DeflateOutputStreambuf::endDeflation()
{
    overflow();

    m_zs.next_out  = reinterpret_cast<unsigned char *>(&m_outvec[0]);
    m_zs.avail_out = getBufferSize();

    // Deflate until _invec is empty.
    int err(Z_OK);

    // make sure to NOT call deflate() if nothing was written to the
    // deflate output stream, otherwise we get a spurious 0x03 0x00
    // marker from the zlib library
    //
    if(m_overflown_bytes > 0)
    {
        while(err == Z_OK)
        {
            if(m_zs.avail_out == 0)
            {
                flushOutvec();
            }

            err = deflate(&m_zs, Z_FINISH);
        }
    }
    else
    {
        err = Z_STREAM_END;
    }

    flushOutvec();

    if(err != Z_STREAM_END)
    {
        std::cerr << "DeflateOutputStreambuf::endDeflation(): deflate() failed: "
                  << zError(err) << std::endl;
    }
}


} // namespace
// vim: ts=4 sw=4 et

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:
