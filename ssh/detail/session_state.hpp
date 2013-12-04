/**
    @file

    RAII lifetime management of libssh2 sessions.

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

#ifndef SSH_DETAIL_SESSION_STATE_HPP
#define SSH_DETAIL_SESSION_STATE_HPP

#include <ssh/detail/libssh2/session.hpp> // init

#include <boost/exception/errinfo_api_function.hpp> // errinfo_api_function
#include <boost/exception/info.hpp> // errinfo_api_function
#include <boost/move/move.hpp> // BOOST_RV_REF
#include <boost/noncopyable.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <exception> // bad_alloc
#include <string>

#include <libssh2.h>

namespace ssh {
namespace detail {

/**
 * RAII object managing session state that must be maintained together.
 *
 * Manages the graceful shutdown/destruction of the session.
 *
 * Unlike a lot of simple allocate-deallocate RAII, this class has to manage
 * an, optional, post-allocation 'starup' stage and ensure that, if started,
 * it is shutdown before de-allocation.  This means that we have to be
 * careful of the lifetime of the unstarted session in the code below.
 * The session may fail to start but must still be freed.
 */
class session_state : private boost::noncopyable
{
    BOOST_MOVABLE_BUT_NOT_COPYABLE(session_state)

public:

    /**
     * Creates a session that is not (and never will be) connected to a host.
     */
    session_state() : m_session(::ssh::detail::libssh2::session::init()) {}

    /**
     * Creates a session connected to a host over the given socket.
     */
    session_state(int socket, const std::string& disconnection_message)
        : m_session(libssh2::session::init())
    {
        // Session is 'alive' from this point onwards.  All paths must
        // eventually free it.

        boost::system::error_code ec;
        std::string error_message;

        libssh2::session::startup(m_session, socket, ec, error_message);

        if (ec)
        {
            // Must free session here as destructor won't be called
            ::libssh2_session_free(m_session);

            BOOST_THROW_EXCEPTION(
                boost::system::system_error(ec, error_message));
        }
        else
        {
            // Setting the disconnection message signals to the destructor
            // that disconnection is necessary
            m_disconnection_message = disconnection_message;
        }
    }

    session_state(BOOST_RV_REF(session_state) other)
        : m_session(boost::move(other.m_session)),
          m_disconnection_message(boost::move(other.m_disconnection_message))
    {}

    ~session_state() throw()
    {
        // Ignoring any errors because there's nothing we can do about them

        if (m_disconnection_message)
        {
            boost::system::error_code ec;
            libssh2::session::disconnect(
                m_session, m_disconnection_message->c_str(), ec);
        }

        ::libssh2_session_free(m_session);
    }

    LIBSSH2_SESSION* session_ptr()
    {
        return m_session;
    }

private:

    LIBSSH2_SESSION* m_session;

    // Overloading this to hold both the message and flag whether disconnection
    // is necessary.
    boost::optional<std::string> m_disconnection_message;

};

}} // namespace ssh::detail

#endif
