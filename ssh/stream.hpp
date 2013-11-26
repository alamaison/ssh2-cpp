/**
    @file

    SSH SFTP file streams.

    @if license

    Copyright (C) 2013  Alexander Lamaison <awl03@doc.ic.ac.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    In addition, as a special exception, the the copyright holders give you
    permission to combine this program with free software programs or the 
    OpenSSL project's "OpenSSL" library (or with modified versions of it, 
    with unchanged license). You may copy and distribute such a system 
    following the terms of the GNU GPL for this program and the licenses 
    of the other code concerned. The GNU General Public License gives 
    permission to release a modified version without this exception; this 
    exception also makes it possible to release a modified version which 
    carries forward this exception.

    @endif
*/

#ifndef SSH_STREAM_HPP
#define SSH_STREAM_HPP
#pragma once

#include <ssh/ssh_error.hpp> // last_error, ssh_error
#include <ssh/session.hpp>
#include <ssh/sftp.hpp>

#include <boost/exception/errinfo_file_name.hpp> // errinfo_file_name
#include <boost/exception/info.hpp> // errinfo_api_function
#include <boost/filesystem/path.hpp> // path
#include <boost/iostreams/categories.hpp>
                               // seekable, input_seekable, output_seekable
#include <boost/iostreams/stream.hpp>
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION, throw_exception

#include <cassert> // assert
#include <stdexcept> // invalid_argument, logic_error
#include <string>

#include <libssh2_sftp.h>

namespace ssh {
namespace sftp {

#define SSH_THROW_LAST_SFTP_ERROR(session, sftp_session, api_function) \
    ::ssh::sftp::detail::throw_last_error( \
        session,sftp_session,BOOST_CURRENT_FUNCTION,__FILE__,__LINE__, \
        api_function)

#define SSH_THROW_LAST_SFTP_ERROR_WITH_PATH( \
    session, sftp_session, api_function, path, path_len) \
    ::ssh::sftp::detail::throw_last_error( \
        session,sftp_session,BOOST_CURRENT_FUNCTION,__FILE__,__LINE__, \
        api_function, path, path_len)

/**
 * Flags defining how to open a file.
 *
 * Using this rather than `std::ios_base::openmode` to allow us to support
 * non-standard `nocreate` and `noreplace`, which correspond to SFTP file modes, as
 * well as eliminating `ate` and `binary` flags which we don't support.
 *
 * The meaning of the standard flags is the same as in
 * `std::ios_base::openmode`.
 */
struct openmode
{
    enum value
    {
        /**
         * Open the file so that it is readable.
         *
         * The file must already exist unless `trunc` is also given in which
         * case a new empty file is created with 0644 permissions.
         */
        in = std::ios_base::in,

        /**
         * Open the file so that it is writable.
         *
         * The file will be created if it does not already exist, unless `in` is
         * also given without `trunc`.  If a new file is created it will empty
         * and have 0644 permissions.
         *
         * If neither `in` not `app` are given, will truncate any existing
         * file (i.e. will have same behaviour as if `trunc` had been given).
         */
        out = std::ios_base::out,

        /**
         * All writes to the file will append to the existing contents.
         *
         * This is more than just opening the file at the end as writes _cannot_
         * modify earlier data even if the file is seeked to an earlier point.
         *
         * @warning  This flag is not supported by common SFTP servers including
         *           the ubiquitous OpenSSH making is pretty useless in
         *           practice.
         */
         app = std::ios_base::app,

        /**
         * Empties the file when opening it.
         *
         * `out` must also be specified for `trunc` to have any effect. `out` without
         * `app` or `in` behaves as if `trunc` had been given, whether or not it is.
         *
         * @todo How does STL `fstream behave if `out` is not specified? Error? Or
         *       just ignore it like we do?
         */
         trunc = std::ios_base::trunc,

        /**
         * Fail if the file does not already exist.
         *
         * `in` without `trunc` has this behaviour whether or not `nocreate` is
         * given.
         */
         nocreate = 0x40,

        /**
         * Fail if the file already exists.
         */
        noreplace = 0x80
    };
};

inline openmode::value operator|(openmode::value l, openmode::value r)
{
    return static_cast<openmode::value>(
        static_cast<int>(l) | static_cast<int>(r));
}

inline openmode::value operator&(openmode::value l, openmode::value r)
{
    return static_cast<openmode::value>(
        static_cast<int>(l) & static_cast<int>(r));
}

inline openmode::value operator^(openmode::value l, openmode::value r)
{
    return static_cast<openmode::value>(
        static_cast<int>(l) ^ static_cast<int>(r));
}

inline openmode::value& operator|=(openmode::value& l, openmode::value r)
{
    return l = l | r;
}

inline openmode::value& operator&=(openmode::value& l, openmode::value r)
{
    return l = l & r;
}

inline openmode::value& operator^=(openmode::value& l, openmode::value r)
{
    return l = l ^ r;
}

// No operator~ as that might not be safe in C++03 where cannot specify enum
// underlying type
// (see http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2010/n3110.html)

namespace detail {

    inline openmode::value translate_flags(std::ios_base::openmode std_mode)
    {
        openmode::value our_mode = openmode::value();

        if (std_mode & std::ios_base::in)
        {
            our_mode |= openmode::in;
        }

        if (std_mode & std::ios_base::out)
        {
            our_mode |= openmode::out;
        }

        if (std_mode & std::ios_base::ate)
        {
            // TODO: support it. Should be simple, all we have to do it seek!
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("ate flag not yet supported"));
        }

        if (std_mode & std::ios_base::app)
        {
            our_mode |= openmode::app;
        }

        if (std_mode & std::ios_base::trunc)
        {
            our_mode |= openmode::trunc;
        }

        if (std_mode & std::ios_base::binary)
        {
            ; // do nothing. our streams are always binary
        }

        return our_mode;
    }

    namespace libssh2 {
    namespace sftp {

        /**
         * Thin exception wrapper around libssh2_sftp_read.
         */
        inline ssize_t read(
            boost::shared_ptr<LIBSSH2_SESSION> session,
            boost::shared_ptr<LIBSSH2_SFTP> sftp,
            boost::shared_ptr<LIBSSH2_SFTP_HANDLE> file_handle,
            char* buffer, size_t buffer_len)
        {
            ssize_t count = libssh2_sftp_read(
                file_handle.get(), buffer, buffer_len);
            if (count < 0)
                SSH_THROW_LAST_SFTP_ERROR(session, sftp, "libssh2_sftp_read");

            return count;
        }

        /**
         * Thin exception wrapper around libssh2_sftp_read.
         */
        inline ssize_t write(
            boost::shared_ptr<LIBSSH2_SESSION> session,
            boost::shared_ptr<LIBSSH2_SFTP> sftp,
            boost::shared_ptr<LIBSSH2_SFTP_HANDLE> file_handle,
            const char* buffer, size_t buffer_len)
        {
            ssize_t count = libssh2_sftp_write(
                file_handle.get(), buffer, buffer_len);
            if (count < 0)
                SSH_THROW_LAST_SFTP_ERROR(session, sftp, "libssh2_sftp_write");

            return count;
        }

    }}

    inline long openmode_to_libssh2_flags(openmode::value opening_mode)
    {
        long flags = 0;

        if (opening_mode & openmode::in)
        {
            flags |= LIBSSH2_FXF_READ;
        }

        if (opening_mode & openmode::out)
        {
            flags |= LIBSSH2_FXF_WRITE;

            if (opening_mode & openmode::in)
            {
                // in flag suppresses creation

                if (opening_mode & openmode::trunc)
                {
                    // but trunk flag unsuppresses it again

                    if (!(opening_mode & openmode::nocreate))
                    {
                        // unless nocreate given in which case just truncate existing
                        flags |= LIBSSH2_FXF_CREAT;

                        if (opening_mode & openmode::noreplace)
                        {
                            flags |= LIBSSH2_FXF_EXCL;
                        }
                    }
                    else if (opening_mode & openmode::noreplace)
                    {
                        BOOST_THROW_EXCEPTION(
                            std::invalid_argument(
                                "Cannot combine nocreate and noreplace"));
                    }

                    // XXX: According to SFTP spec, shouldn't be able to have TRUNC
                    // without CREAT but if it works, it works
                    flags |= LIBSSH2_FXF_TRUNC;
                }
            }
            else
            {
                // Unlike the C and C++ file APIs, SFTP files opened only
                // for writing are not created if they do not already
                // exist and are not truncated if they do exists.  Therefore we
                // explicitly add the CREAT and TRUNC flags to mirror the C++
                // fstream behaviour

                if (!(opening_mode & openmode::nocreate))
                {
                    flags |= LIBSSH2_FXF_CREAT;

                    if (opening_mode & openmode::noreplace)
                    {
                        flags |= LIBSSH2_FXF_EXCL;
                    }
                }
                else if (opening_mode & openmode::noreplace)
                {
                    BOOST_THROW_EXCEPTION(
                        std::invalid_argument(
                            "Cannot combine nocreate and noreplace"));
                }

                if (opening_mode & openmode::app)
                {
                    flags |= LIBSSH2_FXF_APPEND;
                }
                else
                {
                    // XXX: According to SFTP spec, shouldn't be able to have TRUNC
                    // without CREAT but if it works, it works
                    flags |= LIBSSH2_FXF_TRUNC;
                }
            }
        }

        return flags;
    }

    inline boost::shared_ptr<LIBSSH2_SFTP_HANDLE> open_file(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        openmode::value opening_mode)
    {
        std::string path_string = open_path.string();

        // Open with 644 permissions - good for non-directory files
        return detail::libssh2::sftp::open(
            channel.session().get(), channel.get(), path_string.data(),
            path_string.size(), openmode_to_libssh2_flags(opening_mode),
            LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
            LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH,
            LIBSSH2_SFTP_OPENFILE);
    }

    inline boost::shared_ptr<LIBSSH2_SFTP_HANDLE> open_input_file(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        openmode::value opening_mode)
    {
        // For input streams open files for input even if not given in open
        // flags.  Matches standard library ifstream.

        return open_file(channel, open_path, opening_mode | openmode::in);
    }

    inline boost::shared_ptr<LIBSSH2_SFTP_HANDLE> open_output_file(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        openmode::value opening_mode)
    {
        // For output streams open files for output even if not given in open
        // flags.  Matches standard library ofstream.

        return open_file(
            channel, open_path,
            static_cast<openmode::value>(opening_mode | openmode::out));
    }

    inline boost::iostreams::stream_offset seek(
        sftp_channel channel, boost::shared_ptr<LIBSSH2_SFTP_HANDLE> handle,
        const boost::filesystem::path& open_path,
        boost::iostreams::stream_offset off, std::ios_base::seekdir way)
    {
        boost::iostreams::stream_offset new_position = 0;

        switch (way)
        {
        case std::ios_base::beg:
            new_position = off;
            break;

        case std::ios_base::cur:
            {
                // FIXME: possible to get integer overflow on addition?
                new_position = libssh2_sftp_tell64(handle.get()) + off;
                break;
            }

        case std::ios_base::end: // MUST ACCESS SERVER
            {
                LIBSSH2_SFTP_ATTRIBUTES attributes = LIBSSH2_SFTP_ATTRIBUTES();

                int rc = libssh2_sftp_fstat_ex(
                    handle.get(), &attributes, LIBSSH2_SFTP_STAT);
                if (rc != 0)
                {
                    std::string open_path_string = open_path.string();
                    SSH_THROW_LAST_SFTP_ERROR_WITH_PATH(
                        channel.session().get(), channel.get(),
                        "libssh2_sftp_fstat_ex", open_path_string.data(),
                        open_path_string.length());
                }

                new_position = attributes.filesize + off;
                break;
            }

        default:
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("Unknown seek direction"));
        }

        if (new_position < 0)
        {
            BOOST_THROW_EXCEPTION(
                std::logic_error("Cannot seek before start of file"));
        }


        libssh2_sftp_seek64(handle.get(), new_position);

        return new_position;
    }

    inline std::streamsize read(
        sftp_channel channel, boost::shared_ptr<LIBSSH2_SFTP_HANDLE> handle,
        const boost::filesystem::path& open_path,
        char* buffer, std::streamsize buffer_size)
    {
        try
        {
            // This method is only allowed to return a read count less than
            // the requested read amount if the end-of-file has been reached.
            // In other words, non-blocking short reads are not allowed (see
            // http://bit.ly/1ixEagu and http://bit.ly/1ejYm2T).
            // Therefore we loop until all the given buffer has been filled
            // or we reach EOF.

            ssize_t count = 0;
            do
            {
                ssize_t rc = detail::libssh2::sftp::read(
                    channel.session().get(), channel.get(), handle,
                    buffer + count, buffer_size - count);
                if (rc == 0)
                    break; // EOF

                count += rc;
            }
            while (count < buffer_size);

            return count;
        }
        catch (boost::exception& e)
        {
            e << boost::errinfo_file_name(open_path.string());
            throw;
        }
    }

    inline std::streamsize write(
        sftp_channel channel, boost::shared_ptr<LIBSSH2_SFTP_HANDLE> handle,
        const boost::filesystem::path& open_path,
        const char* data, std::streamsize data_size)
    {
        try
        {
            // Despite it's signature, this method is not allowed to return a
            // written count less than the given write amount.  The signature
            // is the way it is so that Boost.IOStreams devices can support
            // non-blocking behaviour in the future, but we use our devices
            // to implement STL streams which don't support non-blocking
            // devices (see http://bit.ly/1ixEagu and http://bit.ly/1ejYm2T).
            // Therefore we loop until all data is written.

            ssize_t count = 0;
            do
            {
                count += detail::libssh2::sftp::write(
                    channel.session().get(), channel.get(), handle,
                    data + count, data_size - count);
            }
            while (count < data_size);

            assert(count == data_size);

            return count;
        }
        catch (boost::exception& e)
        {
            e << boost::errinfo_file_name(open_path.string());
            throw;
        }
    }
}

class sftp_input_device :
    public boost::iostreams::device<boost::iostreams::input_seekable>
{
public:

    sftp_input_device(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        openmode::value opening_mode=openmode::in)
        :
    m_channel(channel), m_open_path(open_path),
    m_handle(detail::open_input_file(m_channel, m_open_path, opening_mode))
    {}

    sftp_input_device(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        std::ios_base::openmode opening_mode)
        :
    m_channel(channel), m_open_path(open_path),
    m_handle(
        detail::open_input_file(
            m_channel, m_open_path, detail::translate_flags(opening_mode)))
    {}

    std::streamsize read(char* buffer, std::streamsize buffer_size)
    {
        return detail::read(
            m_channel, m_handle, m_open_path, buffer, buffer_size);
    }

    boost::iostreams::stream_offset seek(
        boost::iostreams::stream_offset off, std::ios_base::seekdir way)
    {
        return detail::seek(m_channel, m_handle, m_open_path, off, way);
    }

private:
    sftp_channel m_channel;
    boost::filesystem::path m_open_path;
    boost::shared_ptr<LIBSSH2_SFTP_HANDLE> m_handle;

};

/**
 * Input file stream.
 *
 * File is opened according to `openmode` flags but always opened as if
 * `openmode::in` has been specified, regardless of whether it is.
 *
 * By default opened as if `openmode::in` is the only flag specified. File
 * always opened in binary mode.  SFTP does not have a text mode.
 */
typedef boost::iostreams::stream<sftp_input_device> ifstream;

class sftp_output_device :
    public boost::iostreams::device<boost::iostreams::output_seekable>
{
public:

    sftp_output_device(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        openmode::value opening_mode=openmode::out)
        :
    m_channel(channel), m_open_path(open_path),
    m_handle(detail::open_output_file(m_channel, m_open_path, opening_mode))
    {}

    sftp_output_device(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        std::ios_base::openmode opening_mode)
        :
    m_channel(channel), m_open_path(open_path),
    m_handle(
        detail::open_output_file(
            m_channel, m_open_path, detail::translate_flags(opening_mode)))
    {}

    std::streamsize write(const char* data, std::streamsize data_size)
    {
        return detail::write(m_channel, m_handle, m_open_path, data, data_size);
    }

    boost::iostreams::stream_offset seek(
        boost::iostreams::stream_offset off, std::ios_base::seekdir way)
    {
        return detail::seek(m_channel, m_handle, m_open_path, off, way);
    }

private:
    sftp_channel m_channel;
    boost::filesystem::path m_open_path;
    boost::shared_ptr<LIBSSH2_SFTP_HANDLE> m_handle;
};


/**
 * Output file stream.
 *
 * File is opened according to `openmode` flags but always opened as if
 * `openmode::out` has been specified, regardless of whether it is.
 *
 * By default opened as if `openmode::out` is the only flag specified. File
 * always opened in binary mode.  SFTP does not have a text mode.
 */
typedef boost::iostreams::stream<sftp_output_device> ofstream;


class sftp_io_device :
    public boost::iostreams::device<boost::iostreams::seekable>
{
public:

    sftp_io_device(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        openmode::value opening_mode=openmode::in | openmode::out)
        :
    m_channel(channel), m_open_path(open_path),
    m_handle(detail::open_file(m_channel, m_open_path, opening_mode))
    {}

    sftp_io_device(
        sftp_channel channel, const boost::filesystem::path& open_path, 
        std::ios_base::openmode opening_mode)
        :
    m_channel(channel), m_open_path(open_path),
    m_handle(
        detail::open_file(
            m_channel, m_open_path, detail::translate_flags(opening_mode)))
    {}

    std::streamsize read(char* buffer, std::streamsize buffer_size)
    {
        return detail::read(
            m_channel, m_handle, m_open_path, buffer, buffer_size);
    }

    std::streamsize write(const char* data, std::streamsize data_size)
    {
        return detail::write(m_channel, m_handle, m_open_path, data, data_size);
    }

    boost::iostreams::stream_offset seek(
        boost::iostreams::stream_offset off, std::ios_base::seekdir way)
    {
        return detail::seek(m_channel, m_handle, m_open_path, off, way);
    }

private:
    sftp_channel m_channel;
    boost::filesystem::path m_open_path;
    boost::shared_ptr<LIBSSH2_SFTP_HANDLE> m_handle;
};

/**
 * Input/output file stream.
 *
 * By default opened as if `openmode::in` and `openmode::out` are both
 * specified.
 *
 * File always opened in binary mode.  SFTP does not have a text mode.
 */
typedef boost::iostreams::stream<sftp_io_device> fstream;

#undef SSH_THROW_LAST_SFTP_ERROR_WITH_PATH
#undef SSH_THROW_LAST_SFTP_ERROR

}} // namespace ssh::sftp

#endif
