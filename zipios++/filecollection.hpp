#pragma once
#ifndef ZIPIOS_FILECOLLECTION_HPP
#define ZIPIOS_FILECOLLECTION_HPP

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
 * \brief Define the zipios::FileCollection class.
 *
 * This object holds a vector of zipios::FileEntry objects representing
 * a Zip archive or an on disk directory of files.
 */

#include "zipios++/fileentry.hpp"


namespace zipios
{


class FileCollection
{
public:
    typedef std::shared_ptr<FileCollection> pointer_t;
    typedef std::vector<pointer_t>          vector_t;
    typedef std::shared_ptr<std::istream>   stream_pointer_t;

    enum class MatchPath : uint32_t
    {
        IGNORE,
        MATCH
    };

                                    FileCollection(std::string const& filename = "");
                                    FileCollection(FileCollection const& src);
    FileCollection&                 operator = (FileCollection const& src);
    virtual pointer_t               clone() const = 0;
    virtual                         ~FileCollection();

    virtual void                    close();
    virtual FileEntry::vector_t     entries() const;
    virtual FileEntry::pointer_t    getEntry(std::string const& name, MatchPath matchpath = MatchPath::MATCH) const;
    virtual stream_pointer_t        getInputStream(std::string const& entry_name, MatchPath matchpath = MatchPath::MATCH) = 0;
    virtual std::string             getName() const;
    virtual size_t                  size() const;
    bool                            isValid() const;
    virtual void                    mustBeValid() const;
    void                            setMethod(size_t limit, StorageMethod small_storage_method, StorageMethod large_storage_method);
    void                            setLevel(size_t limit, FileEntry::CompressionLevel small_compression_level, FileEntry::CompressionLevel large_compression_level);

protected:
    std::string                     m_filename;
    FileEntry::vector_t             m_entries;
    bool                            m_valid = false;
};


std::ostream & operator << (std::ostream& os, FileCollection const& collection);


} // zipios namespace

// vim: ts=4 sw=4 et

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

#endif
