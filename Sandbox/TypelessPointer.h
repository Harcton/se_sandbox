#pragma once

#include "SpehsEngine/Core/WriteBuffer.h"
#include "SpehsEngine/Core/ReadBuffer.h"
#include <unordered_map>
#include <functional>
#include <stddef.h>


namespace se
{
	class WriteBuffer;
	class ReadBuffer;

	/*
		It's a smart pointer like std::unique_ptr<T>, but without the <T>...
		This is mostly just some experimental code.
		My main gripe with this class is that it currently has to include some ugly serialization stuff in it and its not easily extensible outside the class.
	*/
	class TypelessPointer
	{
	public:

		TypelessPointer() = default;

		template<typename T>
		TypelessPointer(T* t)
		{
			reset(t);
		}

		~TypelessPointer()
		{
			reset();
		}

		TypelessPointer(TypelessPointer&& move)
		{
			swap(move);
		}

		void operator=(TypelessPointer&& move)
		{
			swap(move);
		}

		// No deep copying implemented
		TypelessPointer(const TypelessPointer& copy) = delete;
		void operator=(const TypelessPointer& copy) = delete;

		inline explicit operator bool() const
		{
			return data != nullptr;
		}

		inline bool hasValue() const
		{
			return data != nullptr;
		}

		template<typename T>
		void reset(T* ptr = nullptr)
		{
			reset();
			if (ptr)
			{
				updateTypeData<T>();
				typeHashCode = typeid(T).hash_code();
				data = (std::byte*)ptr;
			}
		}

		void reset()
		{
			if (data)
			{
				const std::unordered_map<size_t, std::function<void(void*)>>::iterator destructorIt = destructorFunctions.find(typeHashCode);
				if (destructorIt != destructorFunctions.end())
				{
					destructorIt->second(data);
				}
				delete[] data;
				data = nullptr;
				typeHashCode = 0;
			}
		}

		inline void swap(TypelessPointer& other)
		{
			std::swap(typeHashCode, other.typeHashCode);
			std::swap(data, other.data);
		}

		template<typename T>
		inline T* get()
		{
			if (typeid(T).hash_code() == typeHashCode)
			{
				return (T*)data;
			}
			else
			{
				return nullptr;
			}
		}

		template<typename T>
		inline const T* get() const
		{
			if (typeid(T).hash_code() == typeHashCode)
			{
				return (const T*)data;
			}
			else
			{
				return nullptr;
			}
		}

		template<typename T>
		inline T* release()
		{
			if (typeid(T).hash_code() == typeHashCode)
			{
				T* released = (T*)data;
				data = nullptr;
				typeHashCode = 0;
				return released;
			}
			else
			{
				return nullptr;
			}
		}

		void write(se::WriteBuffer& buffer) const;
		bool read(se::ReadBuffer& readBuffer);

	private:

		// Default constructor
		template<typename T>
		typename std::enable_if<std::is_default_constructible<T>::value, void>::type addDefaultConstructor()
		{
			const size_t _typeHashCode = typeHashCode;
			defaultConstructorFunctions[typeHashCode] = [_typeHashCode](TypelessPointer& typelessPointer)
			{
				typelessPointer.reset();
				typelessPointer.data = (std::byte*)malloc(sizeof(T));
				new (typelessPointer.data) T();
				typelessPointer.typeHashCode = _typeHashCode;
			};
		}
		template<typename T>
		typename std::enable_if<!std::is_default_constructible<T>::value, void>::type addDefaultConstructor()
		{
		}

		// Destructor
		template<typename T>
		typename std::enable_if<std::is_trivially_destructible<T>::value, void>::type addDestructor()
		{
		}
		template<typename T>
		typename std::enable_if<!std::is_trivially_destructible<T>::value, void>::type addDestructor()
		{
			destructorFunctions[typeHashCode] = [](void* data)
			{
				T& t = dynamic_cast<T&>(*(T*)data);
				t.~T();
			};
		}

		// Write to buffer
		template<typename T>
		typename std::enable_if<
			!std::is_class<T>::value,
				void>::type addWriteToBufferFunction()
		{
			// Is not class
			writeToBufferFunctions[typeHashCode] = [](WriteBuffer& writeBuffer, const void* data)
			{
				const T& t = *((const T*)data);
				writeBuffer.write(t);
			};
		}
		template<typename T>
		typename std::enable_if<
			std::is_class<T>::value &&
			WriteBuffer::has_member_write<T, void(T::*)(WriteBuffer&) const>::value,
				void>::type addWriteToBufferFunction()
		{
			// Is class, has write member function
			writeToBufferFunctions[typeHashCode] = [](WriteBuffer& writeBuffer, const void* data)
			{
				const T& t = *((const T*)data);
				t.write(writeBuffer);
			};
		}
		template<typename T>
		typename std::enable_if<
			std::is_class<T>::value &&
			!WriteBuffer::has_member_write<T, void(T::*)(WriteBuffer&) const>::value &&
			WriteBuffer::has_free_write<T>::value,
				void>::type addWriteToBufferFunction()
		{
			// Is class, doesn't have write member function but has free write function
			writeToBufferFunctions[typeHashCode] = [](WriteBuffer& writeBuffer, const void* data)
			{
				const T& t = *((const T*)data);
				writeToBuffer(writeBuffer, t);
			};
		}
		template<typename T>
		typename std::enable_if<
			std::is_class<T>::value &&
			!WriteBuffer::has_member_write<T, void(T::*)(WriteBuffer&) const>::value &&
			!WriteBuffer::has_free_write<T>::value,
				void>::type addWriteToBufferFunction()
		{
			// Is class, doesn't have write member function or free write function
		}

		// Read from buffer
		template<typename T>
		typename std::enable_if<
			!std::is_class<T>::value,
			void>::type addReadFromBufferFunction()
		{
			// Is not class
			readFromBufferFunctions[typeHashCode] = [](ReadBuffer& readBuffer, void* data)
			{
				T& t = *((T*)data);
				return readBuffer.read(t);
			};
		}
		template<typename T>
		typename std::enable_if<
			std::is_class<T>::value &&
			ReadBuffer::has_member_read<T, bool(T::*)(ReadBuffer&)>::value,
			void>::type addReadFromBufferFunction()
		{
			// Is class, has read member function
			readFromBufferFunctions[typeHashCode] = [](ReadBuffer& readBuffer, void* data)
			{
				T& t = *((T*)data);
				return t.read(readBuffer);
			};
		}
		template<typename T>
		typename std::enable_if<
			std::is_class<T>::value &&
			!ReadBuffer::has_member_read<T, bool(T::*)(ReadBuffer&)>::value&&
			ReadBuffer::has_free_read<T>::value,
			void>::type addReadFromBufferFunction()
		{
			// Is class, doesn't have read member function but has free read function
			readFromBufferFunctions[typeHashCode] = [](ReadBuffer& readBuffer, void* data)
			{
				T& t = *((T*)data);
				return readFromBuffer(readBuffer, t);
			};
		}
		template<typename T>
		typename std::enable_if<
			std::is_class<T>::value &&
			!ReadBuffer::has_member_read<T, bool(T::*)(ReadBuffer&)>::value &&
			!ReadBuffer::has_free_read<T>::value,
			void>::type addReadFromBufferFunction()
		{
			// Is class, doesn't have read member function or free read function
		}

		template<typename T>
		void updateTypeData()
		{
			typeHashCode = typeid(T).hash_code();
			if (destructorFunctions.find(typeHashCode) == destructorFunctions.end())
			{
				typeNames[typeHashCode] = typeid(T).name();
				addDefaultConstructor<T>();
				addDestructor<T>();
				addWriteToBufferFunction<T>();
				addReadFromBufferFunction<T>();
			}
		}

		size_t typeHashCode = 0;
		std::byte* data = nullptr;

		// Function pointers
		static std::unordered_map<size_t, std::string> typeNames;
		static std::unordered_map<size_t, std::function<void(TypelessPointer& typelessPointer)>> defaultConstructorFunctions;
		static std::unordered_map<size_t, std::function<void(void*)>> destructorFunctions;
		static std::unordered_map<size_t, std::function<void(WriteBuffer&, const void*)>> writeToBufferFunctions;
		static std::unordered_map<size_t, std::function<bool(ReadBuffer&, void*)>> readFromBufferFunctions;
	};
}
