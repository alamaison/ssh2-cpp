/**
    @file

    ssh::detail::libssh2 namespace documentation.

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

#ifndef SSH_DETAIL_LIBSSH2_LIBSSH2_HPP
#define SSH_DETAIL_LIBSSH2_LIBSSH2_HPP

namespace ssh {
namespace detail {

/**
 * Exception-throwing wrappers around libssh2 functions.
 *
 * This wrapper functions in this namespace adhere to the following
 * restrictions:
 *
 * - The signature, including the return type, exactly matches the
 *   signature of the wrapped function, with three exceptions:
 *
 *   + it may include the following additonal parameters that are not
 *     in the original signature:
 *      - an `error_code` reference, to receive the details of any
 *        error.
 *      - an optional string reference, to receive the debug message
 *        for an error.
 *      - a session parameter in order to retrieve the last error
 *        from the session.
 *      - in the nested `sftp` namespace only, an SFTP channel
 *        parameter in order to retrieve the last SFTP error
 *        received from the server.
 *   + if the range of return values is reduced (see below) such that
 *     the remaining values simply indicate success, the return type
 *     may be changed to `void`.
 *   + if the range of return values is reduced (see below) such that
 *     the remaining values simply indicate success, an out-param may
 *     be changed to be returned directly.
 *
 * - The behaviour is identical to that of the wrapped function except
 *   that the range of possible return values (via return or
 *   out-parameter) may be reduced by substituting them for
 *   exceptions.
 *
 * - As a consequence of the previous restriction, any resources that
 *   need freeing when returned by the wrapped function, also need
 *   freeing after calling the wrapped version.
 *
 * - No references to the arguments are stored once the wrapper
 *   terminates, whether that termination is by return or by
 *   exception.  In particular, the exception object thrown contains
 *   no shared data.
 *
 * - It is permitted to call these functions from within code that is
 *   non-recursively locked on the given session.  Therefore no
 *   coordination of concurrent threads of execution is performed by
 *   the wrappers and only one thread may call these wrapper functions
 *   (or an libssh2 function) with the same session at any time.
 *
 * Any function not able to adhere to these restrictions is not
 * eligible for inclusion in this namespace.
 *
 * Rationale
 * ---------
 *
 * The main reason for keeping these wrappers here is to make sure any
 * locking we introduce in the future for thread-safety spans both the
 * function call and the code to retieve any error.  This is necessary
 * as otherwise the exception thrown may be from an error caused by
 * another thread's call to a function with the same session (only the
 * details of one error are stored per session).
 *
 * This namespace defines a boundary beyond which all functions behave
 * in the way defined here.  This makes it easier to keep track of
 * session lifetimes as well as where to (and not to) lock the
 * session.
 */
namespace libssh2 {

}}} // namespace ssh::detail::libssh2

#endif
