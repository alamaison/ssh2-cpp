/**
    @file

    Exception wrapper round raw libssh2 session functions.

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

#ifndef SSH_DETAIL_LIBSSH2_SESSION_HPP
#define SSH_DETAIL_LIBSSH2_SESSION_HPP

#include <ssh/ssh_error.hpp> // last_error

#include <boost/exception/info.hpp> // errinfo_api_function
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <exception> // bad_alloc

#include <libssh2.h> // LIBSSH2_SESSION, libssh2_session_*

// See ssh/detail/libssh2/libssh2.hpp for rules governing functions in this
// namespace

namespace ssh {
namespace detail {
namespace libssh2 {
namespace session {

/**
 * Thin exception wrapper around libssh2_session_init.
 */
inline LIBSSH2_SESSION* init()
{
    LIBSSH2_SESSION* session = ::libssh2_session_init_ex(
        NULL, NULL, NULL, NULL);
    if (!session)
        BOOST_THROW_EXCEPTION(
            std::bad_alloc("Failed to allocate new ssh session"));

    return session;
}

/**
 * Thin exception wrapper around libssh2_session_startup.
 */
inline void startup(LIBSSH2_SESSION* session, int socket)
{
    int rc = libssh2_session_startup(session, socket);
    if (rc != 0)
    {
        BOOST_THROW_EXCEPTION(
            last_error(session) <<
            boost::errinfo_api_function("libssh2_session_startup"));
    }
}

/**
 * Thin exception wrapper around libssh2_session_disconnect.
 */
inline void disconnect(
    LIBSSH2_SESSION* session, const char* description)
{
    int rc = libssh2_session_disconnect(session, description);
    if (rc != 0)
    {
        BOOST_THROW_EXCEPTION(
            last_error(session) <<
            boost::errinfo_api_function("libssh2_session_disconnect"));
    }
}

}}}} // namespace ssh::detail::libssh2::session

#endif
