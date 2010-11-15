#if !defined(_PTKXX_PTKXX_HPP_)
#define _PTKXX_PTKXX_HPP_

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>
#include <string>
#include <sstream>

#include <pentek/ptkddr.h>

namespace ptkxx {

class
exception
: public std::exception
{
public:
	exception(std::string msg)
		: message(msg)
	{
	}

	virtual
	~exception() throw()
	{
	}

	virtual const char *
	what() const throw()
	{
		return message.c_str();
	}

private:
	std::string message;

};

namespace sys {

class
device
{
public:
	const std::string &
	get_filename() const
	{
		return filename;
	}

private:
	const std::string filename;

public:
	bool
	is_opened()
	{
		return fd != -1;
	}

public:
	void
	set_throw_exceptions(bool throw_exceptions)
	{
		this->throw_exceptions = throw_exceptions;
	}

	inline bool
	get_throw_exceptions()
	{
		return throw_exceptions;
	}

private:
	bool throw_exceptions;

public:
	void
	set_block_size(std::size_t siz)
	{
		block_size = siz;
	}

	inline std::size_t
	get_block_size()
	{
		return block_size;
	}

private:
	std::size_t block_size;

public:
	bool
	open()
	{
		if (fd == -1)
			fd = ::open(get_filename().c_str(), mode);

		if (fd == -1 && get_throw_exceptions())
			throw ptkxx::exception(get_filename() + ": " + __func__ + ": " + ::strerror(errno));

		return fd != -1;
	}

	void
	close()
	{
		if (fd != -1)
			(void)::close(fd);
		fd = -1;
	}

protected:
	int
	ioctl(int request, void *argp)
	{
		return ::ioctl(fd, request, argp);
	}

	int
	read(void *data, std::size_t len)
	{
		if (get_block_size() == 0)
			return ::read(fd, data, len);

		if (len > INT_MAX)
			len = INT_MAX;

		size_t tord;
		int rd, tot = 0;
		char *p = static_cast<char *>(data);

		while (len > 0) {
			tord = get_block_size();
			if (tord > (len - tot))
				tord = (len - tot);

			rd = ::read(fd, p, tord);
			if (rd < 0) {
				if (tot > 0)
					break;

				return -1;
			} else if (rd == 0) {
				break;
			}

			tot += rd;
			p += rd;
		}

		return tot;
	}

	int
	write(const void *data, std::size_t len)
	{
		if (get_block_size() == 0)
			return ::write(fd, data, len);

		if (len > INT_MAX)
			len = INT_MAX;

		size_t towr;
		int wr, tot = 0;
		const char *p = static_cast<const char *>(data);

		while (len > 0) {
			towr = get_block_size();
			if (towr > (len - tot))
				towr = (len - tot);

			wr = ::write(fd, p, towr);
			if (wr < 0) {
				if (tot > 0)
					break;

				return -1;
			} else if (wr == 0) {
				break;
			}

			tot += wr;
			p += wr;
		}

		return tot;
	}

	int
	seek_beg(off_t offset)
	{
		return ::lseek(fd, offset, SEEK_SET);
	}

	int
	seek_cur(off_t offset)
	{
		return ::lseek(fd, offset, SEEK_CUR);
	}

	int
	seek_end(off_t offset)
	{
		return ::lseek(fd, offset, SEEK_END);
	}

public:
	device(const char *fn)
		: filename(fn)
	{
		fd = -1;
		mode = O_RDWR;
		block_size = 0;
	}

	virtual
	~device()
	{
		close();
	}

private:
	int fd;
	int mode;

};

class
driver
{
public:
	driver(const char *name)
	{
		module = name;
	}

	bool
	load()
	{
		std::string cmd = std::string("exec modprobe ") + module;
		int ret = ::system(cmd.c_str());
		if (ret == 0)
			return true;

		return false;
	}

	bool
	unload()
	{
		std::string cmd = std::string("exec rmmod ") + module;
		int ret = ::system(cmd.c_str());
		if (ret == 0)
			return true;

		return false;
	}

	bool
	is_loaded()
	{
		std::string cmd = std::string("exec grep -q '^") + module + " ' /proc/modules";
		int ret = ::system(cmd.c_str());
		if (ret == 0)
			return true;

		return false;
	}

	const std::string &
	get_name() const
	{
		return module;
	}

private:
	std::string module;

};

}}

namespace ptkxx { namespace data {

class
mcs
{
public:
	const std::vector<unsigned int> &
	get_data_const() const
	{
		return data;
	}

	void
	set_data(const std::vector<unsigned int> &data)
	{
		this->data = data;
	}

private:
	std::vector<unsigned int> data;

public:
	bool
	is_loaded() const
	{
		return data.size() > 0;
	}
};

}}

namespace ptkxx { namespace device {

class
fpga
: public ptkxx::sys::device
{
public:
	fpga(const char *fn)
		: ptkxx::sys::device(fn)
	{
		set_block_size(65536);
	}

public:
	bool
	load(const ptkxx::data::mcs &file)
	{
		return write(&file.get_data_const().at(0), file.get_data_const().size() * sizeof(file.get_data_const().at(0)));
	}

};

class
ctrl
: public ptkxx::sys::device
{
public:
	ctrl(const char *fn)
		: ptkxx::sys::device(fn)
	{
	}

public:
	bool
	read_register(uint32_t addr, uint32_t *val, unsigned int page = 2)
	{
		ARG_PEEKPOKE pp;

		pp.offset = addr;
		pp.page = page;
		pp.mask = 0;

		int ret = ioctl(REGGET, &pp);
		if (ret == 0) {
			*val = pp.value;
		} else {
			*val = ~0;

			if (get_throw_exceptions()) {
				std::ostringstream os;

				os << get_filename() << ": " << __func__ << "(0x" << std::hex << addr << "): ioctl: ";
				if (ret == -1)
					os << ::strerror(errno);
				else
					os << "Unknown error";

				throw ptkxx::exception(os.str());
			}
		}

		return ret == 0;
	}

	uint32_t
	get_register(uint32_t addr, unsigned int page = 2)
	{
		uint32_t val;

		(void)read_register(addr, &val, page);

		return val;
	}

	bool
	set_register(uint32_t addr, uint32_t val, uint32_t mask = 0, unsigned int page = 2)
	{
		ARG_PEEKPOKE pp;

		pp.offset = addr;
		pp.page = page;
		pp.mask = mask;
		pp.value = val;

		int ret = ioctl(REGSET, &pp);
		if (ret != 0 && get_throw_exceptions()) {
			std::ostringstream os;

			os << get_filename() << ": " << __func__ << "(0x" << std::hex << addr << ", 0x" << std::hex << val << "): ioctl: ";
			if (ret == -1)
				os << ::strerror(errno);
			else
				os << "Unknown error";

			throw ptkxx::exception(os.str());
		}

		return ret == 0;
	}

};

class
mem
: public ptkxx::sys::device
{
public:
	mem(const char *fn, std::size_t sz)
		: ptkxx::sys::device(fn), size(sz)
	{
		// The original applications (memget and memset) uses buffers
		// of size of 8k double words (8192 * 4), but I think we try
		// buffers of just 8k bytes. I've had problems with bigger
		// buffers.
		set_block_size(8192);
	}

public:
	std::size_t
	get_size() const
	{
		return size;
	}

	template <typename T> bool
	write(const std::vector<T> data, off_t offset = 0)
	{
		int i;

		i = ioctl(DEPTHSET, reinterpret_cast<void *>((data.size() / 4) + (offset / 4)));
		if (i != 0) {
			if (get_throw_exceptions()) {
				if (i == -1)
					throw ptkxx::exception(get_filename() + ": " + __func__ + ": ioctl: " + ::strerror(errno));
				throw ptkxx::exception(get_filename() + ": " + __func__ + ": ioctl: Unknown error");
			}

			return false;
		}

		i = seek_beg(offset);
		if (i != offset) {
			if (get_throw_exceptions()) {
				if (i == -1)
					throw ptkxx::exception(get_filename() + ": " + __func__ + ": lseek: " + ::strerror(errno));
				throw ptkxx::exception(get_filename() + ": " + __func__ + ": lseek: Could not seek to the proper offset");
			}

			return false;
		}

		int wr = ptkxx::sys::device::write(static_cast<const void *>(&data.at(0)), data.size() * sizeof(T));
		if (wr < 0 || static_cast<unsigned int>(wr) != data.size()) {
			if (get_throw_exceptions()) {
				if (i == -1)
					throw ptkxx::exception(get_filename() + ": " + __func__ + ": write: " + ::strerror(errno));
				throw ptkxx::exception(get_filename() + ": " + __func__ + ": write: Short write");
			}

			return false;
		}

		return true;
	}

private:
	const std::size_t size;

public:
	int
	read(void *buf, std::size_t sz, off_t offset)
	{
		int ret;

		ret = ioctl(DEPTHSET, reinterpret_cast<void *>((offset + sz) / sizeof(uint32_t)));
		if (ret != 0)
			return ret;

		ret = seek_beg(offset);
		if (ret != offset) {
			if (ret >= 0) {
				errno = EPIPE;
				ret = -1;
			}
			return ret;
		}

		return ptkxx::sys::device::read(buf, sz);
	}

public:
	int
	write(const void *buf, std::size_t sz, off_t offset = 0)
	{
		int ret;

		ret = ioctl(DEPTHSET, reinterpret_cast<void *>((offset + sz) / sizeof(uint32_t)));
		if (ret != 0)
			return ret;

		ret = seek_beg(offset);
		if (ret != offset) {
			if (ret >= 0) {
				errno = EPIPE;
				ret = -1;
			}
			return ret;
		}

		return ptkxx::sys::device::write(buf, sz);
	}

};

class
conv
: public ptkxx::sys::device
{
public:
	conv(const char *fn)
		: ptkxx::sys::device(fn)
	{
		set_block_size(4096); // FIXME
	}

public:
	bool
	clear_overrun_counter()
	{
		int i = 0;
		int ret = ioctl(CLROVRCNT, &i);
		if (ret != 0 && get_throw_exceptions())
			throw ptkxx::exception(get_filename() + ": " + __func__ + ": ioctl: " + ::strerror(errno));

		return ret == 0;
	}

public:
	bool
	set_buffer_size(std::size_t readbuf, std::size_t dmabuf)
	{
		BUFFER_CFG cfg;

		cfg.bufno = 0; // Not used on 714x.
		cfg.bufsize = readbuf;
		cfg.intbufsize = dmabuf;
		cfg.physAddr = 0;

		int ret = ioctl(BUFSET, &cfg);
		if (ret != 0 && get_throw_exceptions())
			throw ptkxx::exception(get_filename() + ": " + __func__ + ": ioctl: " + ::strerror(errno));

		return ret == 0;
	}

};

class
adc
: public ptkxx::device::conv
{
public:
	adc(const char *fn)
		: ptkxx::device::conv(fn)
	{
	}

	int
	read(void *data, std::size_t len)
	{
		return ptkxx::sys::device::read(data, len);
	}

};

class
dac
: public ptkxx::device::conv
{
public:
	dac(const char *fn)
		: ptkxx::device::conv(fn)
	{
	}

};

}}

namespace ptkxx { namespace driver {

class
ptk7142
: public ptkxx::sys::driver
{
public:
	ptk7142()
		: ptkxx::sys::driver("ptk7140")
	{
	}

};

}}

namespace ptkxx { namespace board {

class
ptk7142
{
private:
	static std::string
	make_basename(unsigned int board)
	{
		std::ostringstream sbuf;
		sbuf << "/dev/pentek/p7142/" << board;
		return sbuf.str();
	}

public:
	ptk7142(unsigned int board = 0)
		: name("ptk7142"), boardno(board), basename(make_basename(boardno))
	{
		get_mems().resize(3);
		adcs.resize(4);
		dacs.resize(1);

		fpga = new ptkxx::device::fpga((get_basename() + "/fpga").c_str());
		ctrl = new ptkxx::device::ctrl((get_basename() + "/ctrl").c_str());

		for (unsigned int n = 0; n < get_mems().size(); n++) {
			std::size_t size;

			if (n < 2)
				size = 128 * 1024 * 1024;
			else
				size = 256 * 1024 * 1024;

			std::ostringstream sbuf;
			sbuf << get_basename() << "/mem" << n;

			get_mems()[n] = new ptkxx::device::mem(sbuf.str().c_str(), size);
		}

		for (unsigned int n = 0; n < get_adcs().size(); n++) {
			std::ostringstream sbuf;
			sbuf << get_basename() << "/dn/" << n << "BR";

			get_adcs()[n] = new ptkxx::device::adc(sbuf.str().c_str());
		}

		for (unsigned int n = 0; n < get_dacs().size(); n++) {
			std::ostringstream sbuf;
			sbuf << get_basename() << "/up/" << n;

			get_dacs()[n] = new ptkxx::device::dac(sbuf.str().c_str());
		}

		set_throw_exceptions(false);
	}

	virtual
	~ptk7142()
	{
		close();

		for (unsigned int n = 0; n < get_dacs().size(); n++) {
			if (get_dacs()[n] != NULL)
				delete get_dacs()[n];
		}

		for (unsigned int n = 0; n < get_adcs().size(); n++) {
			if (get_adcs()[n] != NULL)
				delete get_adcs()[n];
		}

		for (unsigned int n = 0; n < get_mems().size(); n++) {
			if (get_mems()[n] != NULL)
				delete get_mems()[n];
		}

		delete ctrl;
		delete fpga;
	}

public:
	const std::string &
	get_name() const
	{
		return name;
	}

private:
	const std::string name;

public:
	unsigned int
	get_boardno() const
	{
		return boardno;
	}

private:
	const unsigned int boardno;


public:
	const std::string &
	get_basename() const
	{
		return basename;
	}

private:
	const std::string basename;

public:
	bool
	is_present()
	{
		struct stat st;

		if (stat(get_basename().c_str(), &st) == -1)
			return false;

		if ((st.st_mode & S_IFDIR) != S_IFDIR)
			return false;

		return true;
	}

public:
	void
	set_throw_exceptions(bool throw_exceptions)
	{
		this->throw_exceptions = throw_exceptions;

		get_fpga().set_throw_exceptions(throw_exceptions);
		get_ctrl().set_throw_exceptions(throw_exceptions);

		for (unsigned int n = 0; n < get_mems().size(); n++)
			get_mem(n).set_throw_exceptions(throw_exceptions);

		for (unsigned int n = 0; n < get_adcs().size(); n++)
			get_adc(n).set_throw_exceptions(throw_exceptions);

		for (unsigned int n = 0; n < get_dacs().size(); n++)
			get_dac(n).set_throw_exceptions(throw_exceptions);
	}

	inline bool
	get_throw_exceptions()
	{
		return throw_exceptions;
	}

private:
	bool throw_exceptions;

public:
	ptkxx::device::fpga &
	get_fpga()
	{
		return *fpga;
	}

	ptkxx::device::ctrl &
	get_ctrl()
	{
		return *ctrl;
	}

	std::vector<ptkxx::device::mem *> &
	get_mems()
	{
		return mems;
	}

	std::vector<ptkxx::device::adc *> &
	get_adcs()
	{
		return adcs;
	}

	std::vector<ptkxx::device::dac *> &
	get_dacs()
	{
		return dacs;
	}

	ptkxx::device::mem &
	get_mem(unsigned int n)
	{
		return *mems.at(n);
	}

	ptkxx::device::adc &
	get_adc(unsigned int n)
	{
		return *adcs.at(n);
	}

	ptkxx::device::dac &
	get_dac(unsigned int n)
	{
		return *dacs.at(n);
	}

public:
	void
	close()
	{
		for (unsigned int n = 0; n < get_dacs().size(); n++)
			get_dacs()[n]->close();

		for (unsigned int n = 0; n < get_adcs().size(); n++)
			get_adcs()[n]->close();

		for (unsigned int n = 0; n < get_mems().size(); n++)
			get_mems()[n]->close();

		ctrl->close();
		fpga->close();
	}

public:
	bool
	load_mcs(ptkxx::sys::driver &driver, ptkxx::data::mcs &mcs)
	{
		if (!mcs.is_loaded())
			return false;

		close();

		if (driver.is_loaded() && !driver.unload())
			return false;

		if (!driver.load())
			return false;

		if (!get_fpga().open())
			return false;

		if (!get_fpga().load(mcs))
			return false;

		close();

		if (!driver.unload())
			return false;

		if (!driver.load())
			return false;

		return true;
	}

private:
	ptkxx::device::fpga *fpga;
	ptkxx::device::ctrl *ctrl;
	std::vector<ptkxx::device::mem *> mems;
	std::vector<ptkxx::device::adc *> adcs;
	std::vector<ptkxx::device::dac *> dacs;

};

}}

#endif // !defined(_PTKXX_PTKXX_HPP_)
