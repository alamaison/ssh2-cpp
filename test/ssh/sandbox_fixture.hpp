/**
    @file

    Fixture creating a temporary sandbox directory.

    @if license

    Copyright (C) 2009, 2010  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#ifndef SSH_SANDBOX_FIXTURE_HPP
#define SSH_SANDBOX_FIXTURE_HPP
#pragma once

#include <boost/filesystem.hpp> // path

#include <string>

namespace test {
namespace ssh {

/**
 * Fixture that creates and destroys a sandbox directory.
 */
class sandbox_fixture
{
public:
	sandbox_fixture();
	~sandbox_fixture();

	boost::filesystem::path sandbox();
	boost::filesystem::path new_file_in_sandbox();

private:
	boost::filesystem::path m_sandbox;
};

}} // namespace test::ssh

#endif
