#pragma once
#include "stream.hpp"

namespace nhttp {

	class NHTTP_API file_stream : public stream {
	private:
		FILE* fp;
		bool eof;
		mutable ssize_t size;

	public:
		file_stream(const char* name, const char* mode)
			: fp(fopen(name, mode)), eof(false), size(-1)
		{
			if (fp) {
				struct stat _stat_f;

#ifdef _MSC_VER
#	define nhttp_fileno(fp) _fileno(fp)
#else
#	define nhttp_fileno(fp) fileno(fp)
#endif
				if (!fstat(nhttp_fileno(fp), &_stat_f)) {
					size = _stat_f.st_size;
				}
			}
		}

		~file_stream() { close(); }

	public:
		/* determines validity of this stream. */
		virtual bool is_valid() const override { return fp != nullptr; }

		/**
		 * get total length of this stream.
		 * @returns:
		 *	>= 0: length.
		 *  <  0: not supported.
		 */
		virtual ssize_t get_length() const override {
			if (!fp) return 0;
			if (size < 0) {
				size = stream::get_length();
			}

			return size;
		}

		/* determines currently at end of stream. */
		virtual bool is_end_of() const override {
			ssize_t pos = fp ? tell() : 0;
			return !fp || (pos < 0 ? eof : get_length() <= pos);
		}

		/**
		 * get current position of file cursor.
		 * @returns:
		 *	>= 0: position.
		 *  <  0: not supported.
		 */
		virtual ssize_t tell() const override {
			ssize_t v = fp ? ftell(fp) : 0;

			if (fp) {
				set_errno_c(errno); 
				if (errno) {
					return -1;
				}
			}

			else set_errno_c(ENOENT);

			return v;
		}

		/**
		 * set current position of file cursor.
		 * @params: one of SEEK_SET, SEEK_CUR, SEEK_END
		 * @returns false if not supported.
		 */
		virtual bool seek(ssize_t off, int32_t orig) override {
			if (fp) {
				int32_t ret = fseek(fp, int32_t(off), orig) >= 0;

				if (!ret)
					 set_errno(errno);
				else set_errno(0);

				return !ret;
			}

			return false;
		}

		/**
		 * read bytes from stream.
		 * @returns:
		 *	> 0: read size.
		 *  = 0: end of stream.
		 *  < 0: not supported.
		 */
		virtual int32_t read(void* buf, size_t len) override {
			if (fp) {
				ssize_t s = fread(buf, 1, len, fp);

				if (s <= 0) {
					eof = true;

					if (s < 0)
						 set_errno(errno);
					else set_errno(0);
				}
				
				return int32_t(s);
			}

			return 0;
		}

		/**
		 * wrtie bytes into stream.
		 * @returns:
		 *	> 0: written size.
		 *  = 0: end of stream.
		 *  < 0: not supported.
		 */
		virtual int32_t write(const void* buf, size_t len) override {
			if (fp) {
				int32_t s = int32_t(fwrite(buf, 1, len, fp));
				
				if (s < 0)
					 set_errno(errno);
				else set_errno(0);

				return s;
			}

			return 0;
		}

		/* flush internal buffers. */
		virtual void flush() {
			if (fp) {
				fflush(fp);
				set_errno(0);
			}

			else set_errno(ENOENT);
		}

		/* close stream. */
		virtual void close() { 
			if (fp) {
				fclose(fp);
				set_errno(0);
				fp = nullptr;
			}

			else set_errno(ENOENT);
		}

	};

}