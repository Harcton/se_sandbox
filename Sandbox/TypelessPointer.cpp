#include "stdafx.h"
#include "Sandbox/TypelessPointer.h"

#include "SpehsEngine/Core/WriteBuffer.h"
#include "SpehsEngine/Core/ReadBuffer.h"
#include "SpehsEngine/Core/StringUtilityFunctions.h"


namespace se
{
	std::unordered_map<size_t, std::string> TypelessPointer::typeNames;
	std::unordered_map<size_t, std::function<void(TypelessPointer& typelessPointer)>> TypelessPointer::defaultConstructorFunctions;
	std::unordered_map<size_t, std::function<void(void*)>> TypelessPointer::destructorFunctions;
	std::unordered_map<size_t, std::function<void(WriteBuffer&, const void*)>> TypelessPointer::writeToBufferFunctions;
	std::unordered_map<size_t, std::function<bool(ReadBuffer&, void*)>> TypelessPointer::readFromBufferFunctions;

	void TypelessPointer::write(WriteBuffer& writeBuffer) const
	{
		size_t writtenTypeHashCode = typeHashCode;
		if (typeHashCode != 0 && defaultConstructorFunctions.find(typeHashCode) == defaultConstructorFunctions.end())
		{
			log::warning(formatString("Type is not default constructible: %s. Writing null to stream.", typeNames[typeHashCode].c_str()));
			writtenTypeHashCode = 0;
		}

		const std::unordered_map<size_t, std::function<void(WriteBuffer&, const void*)>>::iterator writeToBufferFunctionIt = writeToBufferFunctions.find(typeHashCode);
		if (writeToBufferFunctionIt == writeToBufferFunctions.end())
		{
			log::warning(formatString("Type does not implement writing to WriteBuffer: %s. Writing null to stream.", typeNames[typeHashCode].c_str()));
			writtenTypeHashCode = 0;
		}

		if (typeHashCode != 0 && readFromBufferFunctions.find(typeHashCode) == readFromBufferFunctions.end())
		{
			log::warning(formatString("Type does not implement reading from ReadBuffer: %s. Writing null to stream.", typeNames[typeHashCode].c_str()));
			writtenTypeHashCode = 0;
		}

		writeBuffer.write(writtenTypeHashCode);
		if (writtenTypeHashCode != 0)
		{
			// Call the write function
			writeToBufferFunctionIt->second(writeBuffer, data);
		}
	}

	bool TypelessPointer::read(ReadBuffer& readBuffer)
	{
		size_t writtenTypeHashCode = 0;
		se_read(readBuffer, writtenTypeHashCode);
		if (writtenTypeHashCode != 0)
		{
			// read function
			std::function<void(TypelessPointer& typelessPointer)>& defaultConstructor = defaultConstructorFunctions[writtenTypeHashCode];
			se_assert(defaultConstructor);
			defaultConstructor(*this);
			se_assert(typeHashCode == writtenTypeHashCode);
			std::function<bool(ReadBuffer&, void*)>& readFromBufferFunction = readFromBufferFunctions[typeHashCode];
			se_assert(readFromBufferFunction);
			return readFromBufferFunction(readBuffer, data);
		}
		else
		{
			reset();
		}
		return true;
	}
}
