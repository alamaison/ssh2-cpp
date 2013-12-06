/**
    @file

    Tests for SFTP streams.

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

#include "sandbox_fixture.hpp" // sandbox_fixture
#include "session_fixture.hpp" // session_fixture

#include <ssh/stream.hpp> // test subject

#include <boost/bind/bind.hpp>
#include <boost/filesystem/fstream.hpp> // ofstream
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/future.hpp> // packaged_task
#include <boost/thread/thread.hpp>

#include <string>
#include <vector>

#include <sys/stat.h>
#include <io.h> // chmod

using ssh::session;
using ssh::filesystem::openmode;
using ssh::filesystem::sftp_filesystem;

using boost::bind;
using boost::filesystem::path;
using boost::packaged_task;
using boost::system::system_error;
using boost::thread;

using test::ssh::sandbox_fixture;
using test::ssh::session_fixture;

using std::runtime_error;
using std::string;
using std::vector;

namespace {

class stream_fixture : public session_fixture, public sandbox_fixture
{
public:

    stream_fixture() : m_filesystem(auth_and_open_sftp())
    {}

    sftp_filesystem filesystem()
    {
        return m_filesystem;
    }

    using sandbox_fixture::new_file_in_sandbox;

    path new_file_in_sandbox(const string& data)
    {
        path p = new_file_in_sandbox();
        boost::filesystem::ofstream s(p);

        s.write(data.data(), data.size());

        return p;
    }

private:

    sftp_filesystem auth_and_open_sftp()
    {
        session s = test_session();
        s.authenticate_by_key_files(
            user(), public_key_path(), private_key_path(), "");

        return s.connect_to_filesystem();
    }

    sftp_filesystem m_filesystem;
};

// the large data must fill more than one stream buffer (currently set to
// 32768 (see DEFAULT_BUFFER_SIZE)

string large_data()
{
    string data;
    for (int i = 0; i < 32000; ++i)
    {
        data.push_back('a');
        data.push_back('m');
        data.push_back('z');
    }

    return data;
}

string large_binary_data()
{
    string data;
    for (int i = 0; i < 32000; ++i)
    {
        data.push_back('a');
        data.push_back('\0');
        data.push_back(-1);
    }

    return data;
}

void make_file_read_only(const path& target)
{
    // Boost.Filesystem 2 has no permissions functions so using POSIX
    // instead.  When using BF v3 we could change to this:
    //
    // permissions(
    //     target, remove_perms | owner_read | others_read | group_read);

    struct stat attributes;
    if (stat(target.string().c_str(), &attributes))
    {
        BOOST_THROW_EXCEPTION(
            boost::system::system_error(
            errno, boost::system::system_category()));
    }

#pragma warning(push)
#pragma warning(disable:4996)
    if (chmod(target.string().c_str(), attributes.st_mode & ~S_IWRITE))
#pragma warning(pop)
    {
        BOOST_THROW_EXCEPTION(
            boost::system::system_error(
            errno, boost::system::system_category()));
    }
}

}

BOOST_AUTO_TEST_SUITE(stream_tests)

BOOST_FIXTURE_TEST_SUITE(istream_tests, stream_fixture)

BOOST_AUTO_TEST_CASE( input_stream_multiple_streams )
{
    path target1 = new_file_in_sandbox();
    path target2 = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream s1(chan, to_remote_path(target1));
    ssh::filesystem::ifstream s2(chan, to_remote_path(target2));
}

BOOST_AUTO_TEST_CASE( input_stream_multiple_streams_to_same_file )
{
    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream s1(chan, to_remote_path(target));
    ssh::filesystem::ifstream s2(chan, to_remote_path(target));
}

BOOST_AUTO_TEST_CASE( input_stream_readable )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ifstream s(filesystem(), to_remote_path(target));

    string bob;

    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "gook");
    BOOST_CHECK(!(s >> bob));
    BOOST_CHECK(s.eof());
}

BOOST_AUTO_TEST_CASE( input_stream_readable_multiple_buffers )
{
    // large enough to span multiple buffers
    string expected_data(large_data());

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream remote_stream(chan, to_remote_path(target));

    string bob;

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(remote_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

// Test with Boost.IOStreams buffer disabled.
// Should call directly to libssh2
BOOST_AUTO_TEST_CASE( input_stream_readable_no_buffer )
{
    string expected_data("gobbeldy gook");

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream remote_stream(
        chan, to_remote_path(target), openmode::in, 0);

    string bob;

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(remote_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( input_stream_readable_binary_data )
{
    string expected_data("gobbledy gook\0after-null\x12\11", 26);

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream remote_stream(chan, to_remote_path(target));

    string bob;

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(remote_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( input_stream_readable_binary_data_multiple_buffers )
{
    // large enough to span multiple buffers
    string expected_data(large_binary_data());

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream remote_stream(chan, to_remote_path(target));

    string bob;

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(remote_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( input_stream_readable_binary_data_stream_op )
{
    string expected_data("gobbledy gook\0after-null\x12\x11", 26);

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream remote_stream(chan, to_remote_path(target));

    string bob;

    BOOST_CHECK(remote_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");

    BOOST_CHECK(remote_stream >> bob);
    const char* gn = "gook\0after-null\x12\x11";
    BOOST_CHECK_EQUAL_COLLECTIONS(bob.begin(), bob.end(), gn, gn+17);
    BOOST_CHECK(!(remote_stream >> bob));
    BOOST_CHECK(remote_stream.eof());
}

BOOST_AUTO_TEST_CASE( input_stream_does_not_create_by_default )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(filesystem(), to_remote_path(target)), system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_opens_read_only_by_default )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    ssh::filesystem::ifstream(filesystem(), to_remote_path(target));
}

BOOST_AUTO_TEST_CASE( input_stream_in_flag_does_not_create )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(
            filesystem(), to_remote_path(target), openmode::in), system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_std_in_flag_does_not_create )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(
        filesystem(), to_remote_path(target), std::ios_base::in), system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_in_flag_opens_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    ssh::filesystem::ifstream(filesystem(), to_remote_path(target), openmode::in);
}

BOOST_AUTO_TEST_CASE( input_stream_out_flag_does_not_create )
{
    // Because ifstream forces in as well as out an in suppresses creation

    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(
        filesystem(), to_remote_path(target), openmode::out), system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_out_flag_fails_to_open_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(filesystem(), to_remote_path(target), openmode::out),
        system_error);
}

BOOST_AUTO_TEST_CASE( input_stream_out_trunc_flag_creates )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ifstream remote_stream(
        filesystem(), to_remote_path(target),
        openmode::out | openmode::trunc);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_std_out_trunc_flag_creates )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ifstream remote_stream(
        filesystem(), to_remote_path(target),
        std::ios_base::out | std::ios_base::trunc);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_out_trunc_nocreate_flag_fails )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::trunc | openmode::nocreate),
        system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_out_trunc_noreplace_flag_fails )
{
    path target = new_file_in_sandbox();

    BOOST_CHECK_THROW(
        ssh::filesystem::ifstream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::trunc | openmode::noreplace),
        system_error);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( input_stream_seek_input_absolute )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ifstream s(filesystem(), to_remote_path(target));
    s.seekg(1, std::ios_base::beg);

    string bob;
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "obbledy");
}

BOOST_AUTO_TEST_CASE( input_stream_seek_input_relative )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ifstream s(filesystem(), to_remote_path(target));
    s.seekg(1, std::ios_base::cur);
    s.seekg(1, std::ios_base::cur);

    string bob;
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "bbledy");
}

BOOST_AUTO_TEST_CASE( input_stream_seek_input_end )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ifstream s(filesystem(), to_remote_path(target));
    s.seekg(-3, std::ios_base::end);

    string bob;
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "ook");
}

BOOST_AUTO_TEST_CASE( input_stream_seek_input_too_far_absolute )
{
    path target = new_file_in_sandbox();

    ssh::filesystem::ifstream s(filesystem(), to_remote_path(target));
    s.exceptions(
        std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit);
    s.seekg(1, std::ios_base::beg);

    string bob;
    BOOST_CHECK_THROW(s >> bob, runtime_error);
}

BOOST_AUTO_TEST_CASE( input_stream_seek_input_too_far_relative )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ifstream s(filesystem(), to_remote_path(target));
    s.exceptions(
        std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit);
    s.seekg(9, std::ios_base::cur);
    s.seekg(4, std::ios_base::cur);

    string bob;
    BOOST_CHECK_THROW(s >> bob, runtime_error);
}

BOOST_AUTO_TEST_SUITE_END();

BOOST_FIXTURE_TEST_SUITE(ofstream_tests, stream_fixture)

BOOST_AUTO_TEST_CASE( output_stream_multiple_streams )
{
    path target1 = new_file_in_sandbox();
    path target2 = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream s1(chan, to_remote_path(target1));
    ssh::filesystem::ofstream s2(chan, to_remote_path(target2));
}

BOOST_AUTO_TEST_CASE( output_stream_multiple_streams_to_same_file )
{
    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream s1(chan, to_remote_path(target));
    ssh::filesystem::ofstream s2(chan, to_remote_path(target));
}

BOOST_AUTO_TEST_CASE( output_stream_writeable )
{
    path target = new_file_in_sandbox();

    {
        ssh::filesystem::ofstream remote_stream(filesystem(), to_remote_path(target));

        remote_stream << "gobbledy gook";
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gook");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_write_multiple_buffers )
{
    // large enough to span multiple buffers
    string data(large_data());

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

// Test with Boost.IOStreams buffer disabled.
// Should call directly to libssh2
BOOST_AUTO_TEST_CASE( output_stream_write_no_buffer )
{
    string data("gobbeldy gook");

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream remote_stream(
        chan, to_remote_path(target), openmode::out, 0);
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_write_binary_data )
{
    string data("gobbledy gook\0after-null\x12\x11", 26);

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_write_binary_data_multiple_buffers )
{
    // large enough to span multiple buffers
    string data(large_binary_data());

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_write_binary_data_stream_op )
{
    string data("gobbledy gook\0after-null\x12\x11", 26);

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ofstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream << data);
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}


BOOST_AUTO_TEST_CASE( output_stream_creates_by_default )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(filesystem(), to_remote_path(target));
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_nocreate_flag )
{
    path target = new_file_in_sandbox();

    ssh::filesystem::ofstream(
        filesystem(), to_remote_path(target), openmode::nocreate);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_nocreate_flag_fails )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target), openmode::nocreate),
        system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_noreplace_flag )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream(
        filesystem(), to_remote_path(target), openmode::noreplace);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_noreplace_flag_fails )
{
    path target = new_file_in_sandbox();

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target), openmode::noreplace),
        system_error);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_flag_creates )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target), openmode::out);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_flag_truncates )
{
    path target = new_file_in_sandbox("gobbledy gook");

    {
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target), openmode::out);
        BOOST_CHECK(exists(target));

        BOOST_CHECK(remote_stream << "abcdef");
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "abcdef");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_out_nocreate_flag )
{
    path target = new_file_in_sandbox();

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target), openmode::out | openmode::nocreate);

    BOOST_CHECK(remote_stream << "abcdef");
}

BOOST_AUTO_TEST_CASE( output_stream_out_nocreate_flag_fails )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::nocreate),
        system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_noreplace_flag )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target), openmode::out | openmode::noreplace);

    BOOST_CHECK(exists(target));
    BOOST_CHECK(remote_stream << "abcdef");
}

BOOST_AUTO_TEST_CASE( output_stream_out_noreplace_flag_fails )
{
    path target = new_file_in_sandbox();

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::noreplace),
        system_error);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_in_flag_does_not_create )
{
    // In flag suppresses creation.  Matches standard lib ofstream.

    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target), openmode::in),
        system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_in_out_does_not_create )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target),
            openmode::in | openmode::out), system_error);

    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_in_out_flag_updates )
{
    // Unlike the out flag for output-only streams, which truncates, the
    // out flag on an input stream leaves the existing contents because the
    // input stream forces the in flag and in|out means update existing

    path target = new_file_in_sandbox("gobbledy gook");

    {
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target),
            openmode::in | openmode::out);
        BOOST_CHECK(exists(target));

        BOOST_CHECK(remote_stream << "abcdef");
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "abcdefdy");

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gook");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_out_trunc_flag_creates )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target),
        openmode::out | openmode::trunc);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_trunc_nocreate_flag )
{
    path target = new_file_in_sandbox();

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target),
        openmode::out | openmode::trunc | openmode::nocreate);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_trunc_nocreate_flag_fails )
{
    path target = new_file_in_sandbox();
    remove(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::trunc | openmode::nocreate),
        system_error);
    BOOST_CHECK(!exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_trunc_noreplace_flag )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target),
        openmode::out | openmode::trunc | openmode::noreplace);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_trunc_noreplace_flag_fails )
{
    path target = new_file_in_sandbox();

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::trunc | openmode::noreplace),
        system_error);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_trunc_flag_truncates )
{
    path target = new_file_in_sandbox("gobbledy gook");

    {
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::trunc);

        BOOST_CHECK(remote_stream << "abcdef");
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "abcdef");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_in_out_trunc_flag_creates )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target),
        openmode::in | openmode::out | openmode::trunc);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_in_out_trunc_flag_truncates )
{
    path target = new_file_in_sandbox("gobbledy gook");

    {
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target),
            openmode::in | openmode::out | openmode::trunc);

        BOOST_CHECK(remote_stream << "abcdef");
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "abcdef");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_out_append_flag_creates )
{
    path target = new_file_in_sandbox();
    remove(target);

    ssh::filesystem::ofstream remote_stream(
        filesystem(), to_remote_path(target),
        openmode::out | openmode::app);
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_CASE( output_stream_out_append_flag_appends )
{
    path target = new_file_in_sandbox("gobbledy gook");

    {
        ssh::filesystem::ofstream remote_stream(
            filesystem(), to_remote_path(target),
            openmode::out | openmode::app);

        BOOST_CHECK(remote_stream << "abcdef");
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    /*
    XXX: Should be as follows but OpenSSH doesn't support FXF_APPEND

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbeldy");

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gookabcdef");
    */

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "abcdefdy");

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gook");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( output_stream_fails_to_open_read_only_by_default )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(filesystem(), to_remote_path(target)), system_error);
}

BOOST_AUTO_TEST_CASE( output_stream_out_flag_fails_to_open_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(filesystem(), to_remote_path(target), openmode::out),
        system_error);
}

BOOST_AUTO_TEST_CASE( output_stream_in_out_flag_fails_to_open_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target),  openmode::in | openmode::out),
        system_error);
}

// Because output streams force out flag, they can't open read-only files
BOOST_AUTO_TEST_CASE( output_stream_in_flag_fails_to_open_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::ofstream(
            filesystem(), to_remote_path(target),  openmode::in), system_error);
}

// By default ostreams overwrite the file so seeking will cause subsequent
// output to write after the file end.  The skipped bytes should be filled
// with NUL
BOOST_AUTO_TEST_CASE( output_stream_seek_output_absolute_overshoot )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target));
    s.seekp(2, std::ios_base::beg);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string expected_data("\0\0r", 3);

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( output_stream_seek_output_absolute )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target), openmode::in);
    s.seekp(1, std::ios_base::beg);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "grbbledy");
}

// By default ostreams overwrite the file so seeking will cause subsequent
// output to write after the file end.  The skipped bytes should be filled
// with NUL
BOOST_AUTO_TEST_CASE( output_stream_seek_output_relative_overshoot )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target));
    s.seekp(1, std::ios_base::cur);
    s.seekp(1, std::ios_base::cur);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string expected_data("\0\0r", 3);

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( output_stream_seek_output_relative )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target), openmode::in);
    s.seekp(1, std::ios_base::cur);
    s.seekp(1, std::ios_base::cur);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gorbledy");
}


// By default ostreams overwrite the file.  Seeking TO the end of this empty
// file will just start writing from the beginning.  No NUL bytes are
// inserted anywhere
BOOST_AUTO_TEST_CASE( output_stream_seek_output_end )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target));
    s.seekp(0, std::ios_base::end);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "r");
    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK_EQUAL(bob, "r");
}

// By default ostreams overwrite the file.  Seeking past the end  will cause
// subsequent output to write after the file end.  The skipped bytes will
// be filled with NUL.
BOOST_AUTO_TEST_CASE( output_stream_seek_output_end_overshoot )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target));
    s.seekp(3, std::ios_base::end);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string expected_data("\0\0\0r", 4);

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( output_stream_seek_output_before_end )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::ofstream s(filesystem(), to_remote_path(target), openmode::in);
    s.seekp(-3, std::ios_base::end);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");
    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "grok");
}

BOOST_AUTO_TEST_SUITE_END();


BOOST_FIXTURE_TEST_SUITE(fstream_tests, stream_fixture)

BOOST_AUTO_TEST_CASE( io_stream_multiple_streams )
{
    path target1 = new_file_in_sandbox();
    path target2 = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream s1(chan, to_remote_path(target1));
    ssh::filesystem::fstream s2(chan, to_remote_path(target2));
}

BOOST_AUTO_TEST_CASE( io_stream_multiple_streams_to_same_file )
{
    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream s1(chan, to_remote_path(target));
    ssh::filesystem::fstream s2(chan, to_remote_path(target));
}

BOOST_AUTO_TEST_CASE( io_stream_fails_to_open_read_only_by_default )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::fstream(filesystem(), to_remote_path(target)), system_error);
}

BOOST_AUTO_TEST_CASE( io_stream_out_flag_fails_to_open_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::fstream(filesystem(), to_remote_path(target), openmode::out),
        system_error);
}

BOOST_AUTO_TEST_CASE( io_stream_in_out_flag_fails_to_open_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    BOOST_CHECK_THROW(
        ssh::filesystem::fstream(
        filesystem(), to_remote_path(target),  openmode::in | openmode::out),
        system_error);
}

BOOST_AUTO_TEST_CASE( io_stream_in_flag_opens_read_only )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    ssh::filesystem::fstream(filesystem(), to_remote_path(target),  openmode::in);
}

BOOST_AUTO_TEST_CASE( io_stream_readable )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));

    string bob;

    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "gook");
    BOOST_CHECK(!(s >> bob));
    BOOST_CHECK(s.eof());
}

BOOST_AUTO_TEST_CASE( io_stream_readable_binary_data )
{
    string expected_data("gobbledy gook\0after-null\x12\11", 26);

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream remote_stream(chan, to_remote_path(target));

    string bob;

    vector<char> buffer(expected_data.size());
    BOOST_CHECK(remote_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(),
        expected_data.begin(), expected_data.end());
}

BOOST_AUTO_TEST_CASE( io_stream_readable_binary_data_stream_op )
{
    string expected_data("gobbledy gook\0after-null\x12\x11", 26);

    path target = new_file_in_sandbox(expected_data);

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream remote_stream(chan, to_remote_path(target));

    string bob;

    BOOST_CHECK(remote_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");

    BOOST_CHECK(remote_stream >> bob);
    const char* gn = "gook\0after-null\x12\x11";
    BOOST_CHECK_EQUAL_COLLECTIONS(bob.begin(), bob.end(), gn, gn+17);
    BOOST_CHECK(!(remote_stream >> bob));
    BOOST_CHECK(remote_stream.eof());
}

BOOST_AUTO_TEST_CASE( io_stream_writeable )
{
    path target = new_file_in_sandbox();

    {
        ssh::filesystem::fstream remote_stream(filesystem(), to_remote_path(target));

        remote_stream << "gobbledy gook";
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gook");

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( io_stream_write_multiple_buffers )
{
    // large enough to span multiple buffers
    string data(large_data());

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

// Test with Boost.IOStreams buffer disabled.
// Should call directly to libssh2
BOOST_AUTO_TEST_CASE( io_stream_write_no_buffer )
{
    string data("gobbeldy gook");

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream remote_stream(
        chan, to_remote_path(target), openmode::in | openmode::out, 0);
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

// An IO stream may be able to open a read-only file when given the in flag,
// but it should still fail to write to it
BOOST_AUTO_TEST_CASE( io_stream_read_only_write_fails )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target),  openmode::in);

    BOOST_CHECK(s << "gobbledy gook");
    BOOST_CHECK(!s.flush()); // Failure happens on the flush

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK_EQUAL(bob, string());
    BOOST_CHECK(local_stream.eof());
}

// Flush is not called explicitly so failure will happen in destructor
BOOST_AUTO_TEST_CASE( io_stream_read_only_write_fails_no_flush )
{
    path target = new_file_in_sandbox();
    make_file_read_only(target);

    {
        ssh::filesystem::fstream s(filesystem(), to_remote_path(target),  openmode::in);

        BOOST_CHECK(s << "gobbledy gook");

        // No explicit flush
    }

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(!(local_stream >> bob));
    BOOST_CHECK_EQUAL(bob, string());
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( io_stream_write_binary_data )
{
    string data("gobbledy gook\0after-null\x12\x11", 26);

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream.write(data.data(), data.size()));
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( io_stream_write_binary_data_stream_op )
{
    string data("gobbledy gook\0after-null\x12\x11", 26);

    path target = new_file_in_sandbox();

    sftp_filesystem chan = filesystem();

    ssh::filesystem::fstream remote_stream(chan, to_remote_path(target));
    BOOST_CHECK(remote_stream << data);
    remote_stream.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    vector<char> buffer(data.size());
    BOOST_CHECK(local_stream.read(&buffer[0], buffer.size()));

    BOOST_CHECK_EQUAL_COLLECTIONS(
        buffer.begin(), buffer.end(), data.begin(), data.end());

    BOOST_CHECK(!local_stream.read(&buffer[0], buffer.size()));
    BOOST_CHECK(local_stream.eof());
}

BOOST_AUTO_TEST_CASE( io_stream_seek_input_absolute )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekg(1, std::ios_base::beg);

    string bob;
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "obbledy");
}

BOOST_AUTO_TEST_CASE( io_stream_seek_input_relative )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekg(1, std::ios_base::cur);
    s.seekg(1, std::ios_base::cur);

    string bob;
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "bbledy");
}

BOOST_AUTO_TEST_CASE( io_stream_seek_input_end )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekg(-3, std::ios_base::end);

    string bob;
    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "ook");
}

BOOST_AUTO_TEST_CASE( io_stream_seek_input_too_far_absolute )
{
    path target = new_file_in_sandbox();

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.exceptions(
        std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit);
    s.seekg(1, std::ios_base::beg);

    string bob;
    BOOST_CHECK_THROW(s >> bob, runtime_error);
}

BOOST_AUTO_TEST_CASE( io_stream_seek_input_too_far_relative )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.exceptions(
        std::ios_base::badbit | std::ios_base::eofbit | std::ios_base::failbit);
    s.seekg(9, std::ios_base::cur);
    s.seekg(4, std::ios_base::cur);

    string bob;
    BOOST_CHECK_THROW(s >> bob, runtime_error);
}

BOOST_AUTO_TEST_CASE( io_stream_seek_output_absolute )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekp(1, std::ios_base::beg);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "grbbledy");
}

BOOST_AUTO_TEST_CASE( io_stream_seek_output_relative )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekp(1, std::ios_base::cur);
    s.seekp(1, std::ios_base::cur);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gorbledy");
}

BOOST_AUTO_TEST_CASE( io_stream_seek_output_end )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekp(-3, std::ios_base::end);

    BOOST_CHECK(s << "r");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    string bob;

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "gobbledy");
    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "grok");
}

BOOST_AUTO_TEST_CASE( io_stream_seek_interleaved )
{
    path target = new_file_in_sandbox("gobbledy gook");

    ssh::filesystem::fstream s(filesystem(), to_remote_path(target));
    s.seekp(1, std::ios_base::beg);

    BOOST_CHECK(s << "r");

    s.seekg(2, std::ios_base::cur);

    string bob;

    BOOST_CHECK(s >> bob);
    // not "bbledy" because read and write head are combined
    BOOST_CHECK_EQUAL(bob, "ledy");

    s.seekp(-4, std::ios_base::end);

    BOOST_CHECK(s << "ahh");

    BOOST_CHECK(s >> bob);
    BOOST_CHECK_EQUAL(bob, "k");

    s.flush();

    boost::filesystem::ifstream local_stream(target);

    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "grbbledy");
    BOOST_CHECK(local_stream >> bob);
    BOOST_CHECK_EQUAL(bob, "ahhk");
}


BOOST_AUTO_TEST_SUITE_END();

BOOST_FIXTURE_TEST_SUITE(threading_tests, stream_fixture)

namespace {

    string get_first_token(ssh::filesystem::ifstream& stream)
    {
        string r;
        stream >> r;
        return r;
    }

}

BOOST_AUTO_TEST_CASE( stream_read_on_different_threads )
{
    path target1 = new_file_in_sandbox("humpty dumpty sat");
    path target2 = new_file_in_sandbox("on the wall");

    sftp_filesystem chan = filesystem();

    ssh::filesystem::ifstream s1(chan, to_remote_path(target1));
    ssh::filesystem::ifstream s2(chan, to_remote_path(target2));

    packaged_task<string> p1(boost::bind(get_first_token, boost::ref(s1)));
    packaged_task<string> p2(boost::bind(get_first_token, boost::ref(s2)));

    thread(boost::ref(p1)).detach();
    thread(boost::ref(p2)).detach();

    BOOST_CHECK_EQUAL(p1.get_future().get(), "humpty");
    BOOST_CHECK_EQUAL(p2.get_future().get(), "on");
}

// There was a bug in our session locking that meant we locked the session
// when opening a file but didn't when closing it (because it happened in
// the shared_ptr destructor).  This test case triggers that bug by opening
// a file (locks and unlocks session), starting to read from a second file
// (locks) session and then closing the first file.  This will cause all
// sorts of bad behaviour of the closure doesn't lock the session so we can
// detect it if it regresses.
BOOST_AUTO_TEST_CASE( parallel_file_closing )
{
    string data = large_data();

    path read_me = new_file_in_sandbox(data);
    path test_me = new_file_in_sandbox();

    ssh::filesystem::ifstream stream1(filesystem(), to_remote_path(read_me));
    ssh::filesystem::ifstream stream2(filesystem(), to_remote_path(test_me));

    // Using a long-running stream read operation to make sure the session
    // is still locked when we try to close the other file
    packaged_task<string> ps(bind(get_first_token, boost::ref(stream1)));
    thread(boost::ref(ps)).detach();

    thread(bind(&ssh::filesystem::ifstream::close, &stream2)).detach();

    BOOST_CHECK_EQUAL(ps.get_future().get(), data);
}

BOOST_AUTO_TEST_SUITE_END();

BOOST_AUTO_TEST_SUITE_END();
