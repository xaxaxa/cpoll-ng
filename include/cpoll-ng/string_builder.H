#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <string.h>

namespace CP
{
	// CharT must be a POD type
	template<class CharT>
	class basic_string_builder {
	public:
		typedef CharT value_type;
		typedef uint32_t size_type;
		typedef CharT& reference;
		typedef CharT* pointer;
		typedef CharT* iterator;
		typedef CharT* reverse_iterator;
		typedef const CharT& const_reference;
		typedef const CharT* const_pointer;
		typedef const CharT* const_iterator;
		typedef const CharT* const_reverse_iterator;

		static constexpr size_type minCapacity = 256;

		CharT* buffer;
		size_type bufferLength;		// how many characters in the string, excluding null terminator
		size_type bufferCapacity;	// size of the buffer
		static const CharT _nullChar = 0;
		static constexpr size_type npos = (size_type) -1;

		// constructors

		basic_string_builder() {
			_init();
		}
		~basic_string_builder() {
			if(buffer != nullptr) free(buffer);
		}
		basic_string_builder(size_type count, CharT ch) {
			_init(count);
			if(sizeof(CharT) == 1)
				memset(buffer, 0, sizeof(CharT) * count);
			else {
				auto* end = buffer + bufferLength;
				for(auto* it = buffer; it != end; it++)
					(*it) = ch;
			}
		}
		basic_string_builder(const basic_string_builder<CharT>& other) {
			_init(other.data(), other.length());
		}
		basic_string_builder(const std::basic_string<CharT>& other) {
			_init(other.data(), other.length());
		}
		basic_string_builder(const std::basic_string_view<CharT>& other) {
			_init(other.data(), other.length());
		}
		// move constructor
		basic_string_builder(basic_string_builder<CharT>&& other) {
			buffer = other.buffer;
			bufferLength = other.bufferLength;
			bufferCapacity = other.bufferCapacity;
			other._init();
		}
		basic_string_builder(const CharT* s, size_type count) {
			_init(s, count);
		}
		basic_string_builder(const CharT* s): basic_string_builder(s, strlen((char*)s)/sizeof(CharT)) {}
		
		basic_string_builder<CharT>& operator=(const basic_string_builder<CharT>& other) {
			_assign(other.data(), other.length()); return *this;
		}
		basic_string_builder<CharT>& operator=(const std::basic_string<CharT>& other) {
			_assign(other.data(), other.length()); return *this;
		}
		basic_string_builder<CharT>& operator=(const std::basic_string_view<CharT>& other) {
			_assign(other.data(), other.length()); return *this;
		}
		basic_string_builder<CharT>& operator=(const CharT* s) {
			_assign(s, strlen((char*)s)/sizeof(CharT)); return *this;
		}

		iterator begin() { return buffer; };
		iterator end() { return buffer + bufferLength; }
		const_iterator begin() const { return buffer; };
		const_iterator end() const { return buffer + bufferLength; }
		const_iterator cbegin() const { return buffer; };
		const_iterator cend() const { return buffer + bufferLength; }

		inline CharT& at(size_type i) {
			if(i < 0 || i >= bufferLength) throw std::out_of_range("string_builder .at() out of bounds");
			return buffer[i];
		}
		inline CharT& operator[](size_type i) noexcept { return buffer[i]; }
		inline CharT& front() noexcept { return *buffer; }
		inline CharT& back() noexcept { return buffer[bufferLength-1]; }
		inline CharT* data() noexcept { return buffer; }

		inline const CharT& operator[](size_type i) const noexcept { return buffer[i]; }
		inline const CharT& front() const noexcept { return *buffer; }
		inline const CharT& back() const noexcept { return buffer[bufferLength-1]; }
		inline const CharT* data() const noexcept { return buffer; }
		inline CharT* c_str() noexcept {
			if(bufferCapacity == 0) return (CharT*) &_nullChar;
			buffer[bufferLength] = 0;
			return buffer;
		}
		inline operator std::basic_string_view<CharT>() const noexcept { return {buffer, bufferLength}; }

		inline bool empty() const noexcept { return bufferLength == 0; }
		inline size_type length() const noexcept { return bufferLength; }
		inline size_type size() const noexcept { return bufferLength; }
		inline size_type max_size() const noexcept { return (1<<30)-2; }
		inline size_type capacity() const noexcept { return bufferCapacity-1; }

		std::basic_string_view<CharT> substr(size_type pos = 0, size_type count = npos) {
			if(pos < 0 || pos > bufferLength) throw std::out_of_range("string_builder .substr() out of bounds");
			if(count == npos) return { buffer + pos, bufferLength - pos };
			if((pos + count) >= bufferLength) throw std::out_of_range("string_builder .substr() out of bounds");
			return { buffer + pos, count };
		}

		template<class T> basic_string_builder<CharT>& operator+=(const T& value) {
			append(value);
			return *this;
		}
		inline void append(const basic_string_builder<CharT>& s) {
			append(s.data(), s.length());
		}
		inline void append(const std::basic_string<CharT>& s) {
			append(s.data(), s.length());
		}
		inline void append(const std::basic_string_view<CharT>& s) {
			append(s.data(), s.length());
		}
		inline void append(const CharT* s) {
			append(s, strlen((char*)s)/sizeof(CharT));
		}
		inline void append(const CharT* s, const CharT* end) {
			append(s, end - s);
		}
		void append(const CharT* s, size_type count) {
			reserve(bufferLength + count);
			memcpy(buffer + bufferLength, s, count * sizeof(CharT));
			bufferLength += count;
		}
		inline void append(CharT c) {
			if((bufferLength + 2) >= bufferCapacity) reserve(bufferLength + 1);
			buffer[bufferLength] = c;
			bufferLength++;
		}

		template<class... P> inline size_type append_format(const char* fmt, P&&... p) {
			CharT* s = begin_append(32);
			size_type space = bufferCapacity - bufferLength;
			int len = snprintf(s, space, fmt, std::forward<P>(p)...);
			// len excludes the null byte, but space includes it
			if(len >= space) {
				s = begin_append(len);
				snprintf(s, len + 1, fmt, std::forward<P>(p)...);
			}
			end_append(len);
			return len;
		}

		CharT* begin_append(size_type count) {
			reserve(bufferLength + count);
			return buffer + bufferLength;
		}
		void end_append(size_type count) {
			bufferLength += count;
		}


		void clear() {
			bufferLength = 0;
		}

		void reserve(size_type count) {
			if((count + 1) <= bufferCapacity) return;
			//fprintf(stderr, "stringbuilder upsize\n");
			size_type newCapacity = (bufferCapacity < minCapacity) ? minCapacity : bufferCapacity;
			while(newCapacity < (count + 1))
				newCapacity *= 2;

			// realloc will call malloc if buffer is nullptr
			CharT* newBuf = (CharT*) realloc(buffer, newCapacity * sizeof(CharT));
			if(newBuf == nullptr) throw std::bad_alloc();
			buffer = newBuf;
			bufferCapacity = newCapacity;
		}
		void resize(size_type count) {
			reserve(count);
			bufferLength = count;
		}

	protected:
		void _init() {
			//fprintf(stderr, "stringbuilder()\n");
			buffer = nullptr;
			bufferLength = bufferCapacity = 0;
		}
		void _init(size_type length) {
			//fprintf(stderr, "stringbuilder()\n");
			bufferCapacity = minCapacity;
			while(bufferCapacity < (length + 1))
				bufferCapacity *= 2;
			buffer = (CharT*) malloc(sizeof(CharT) * bufferCapacity);
			if(buffer == nullptr)
				throw std::bad_alloc();
			bufferLength = length;
		}
		void _init(const CharT* s, size_type count) {
			_init(count);
			memcpy(buffer, s, sizeof(CharT) * count);
		}
		void _assign(const CharT* s, size_type count) {
			resize(count);
			memcpy(buffer, s, sizeof(CharT) * count);
		}
	};

	typedef basic_string_builder<char> string_builder;
}
