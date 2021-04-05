#include "stdafx.h"

#include "SpehsEngine/Audio/AudioLib.h"
#include "SpehsEngine/Core/CoreLib.h"
#include "SpehsEngine/Core/DeltaTimeSystem.h"
#include "SpehsEngine/Core/StringOperations.h"
#include "SpehsEngine/Core/Inifile.h"
#include "SpehsEngine/Core/Console.h"
#include "SpehsEngine/Core/Thread.h"
#include "SpehsEngine/Core/TypelessPointer.h"
#include "SpehsEngine/Core/WriteBuffer.h"
#include "SpehsEngine/Core/ReadBuffer.h"
#include "SpehsEngine/Math/MathLib.h"
#include "SpehsEngine/Net/NetLib.h"
#include "SpehsEngine/Net/ConnectionManager.h"
#include "SpehsEngine/Net/IOService.h"
#include "SpehsEngine/Net/AddressUtilityFunctions.h"
#include "SpehsEngine/Rendering/RenderingLib.h"
#include "SpehsEngine/Rendering/Camera2D.h"
#include "SpehsEngine/Rendering/ConsoleVisualizer.h"
#include "SpehsEngine/Rendering/BatchManager.h"
#include "SpehsEngine/Rendering/ShaderManager.h"
#include "SpehsEngine/Rendering/TextureManager.h"
#include "SpehsEngine/Rendering/Text.h"
#include "SpehsEngine/Rendering/Window.h"
#include "SpehsEngine/Input/InputLib.h"
#include "SpehsEngine/Input/InputManager.h"
#include "SpehsEngine/Input/EventCatcher.h"
#include "SpehsEngine/Input/EventSignaler.h"
#include "SpehsEngine/Physics/PhysicsLib.h"
#include "SpehsEngine/GUI/GUILib.h"
#include "SpehsEngine/GUI/GUIRectangle.h"
#include "SpehsEngine/Debug/DebugLib.h"
#include "SpehsEngine/Debug/ConnectionProfiler.h"
#include "SpehsEngine/Debug/ScopeProfilerVisualizer.h"
#include <thread>


struct Destructible
{
	Destructible()
	{
		se::log::info("Destructible()");
	}
	Destructible(int _i)
		: i(_i)
	{
		se::log::info("Destructible(int" + std::to_string(i) + ")");
	}
	Destructible(const Destructible& other)
	{
		se::log::info("Destructible(copy" + std::to_string(i) + ")");
	}
	Destructible(Destructible&& other)
	{
		se::log::info("Destructible(move" + std::to_string(i) + ")");
	}
	virtual ~Destructible()
	{
		se::log::info("~Destructible(" + std::to_string(i) + ")");
	}
	int i = 0;
};

struct Destructible2 : public Destructible
{
	Destructible2() = default;
	Destructible2(int _i)
		: Destructible(_i)
	{
	}
	~Destructible2()
	{
		se::log::info("~Destructible2(" + std::to_string(i) + ")");
	}
};


struct MemberStreamable
{
	void write(se::WriteBuffer& writeBuffer) const
	{
		se_write(writeBuffer, i);
	}
	bool read(se::ReadBuffer& readBuffer)
	{
		se_read(readBuffer, i);
		return true;
	}
	MemberStreamable() = default;
	MemberStreamable(int _i)
		: i(_i)
	{
	}
	int i = 0;
};
struct FreeStreamable
{
	FreeStreamable() = default;
	FreeStreamable(int _i)
		: i(_i)
	{
	}
	int i = 0;
};
void writeToBuffer(se::WriteBuffer& writeBuffer, const FreeStreamable& t)
{
	se_write(writeBuffer, t.i);
}
bool readFromBuffer(se::ReadBuffer& readBuffer, FreeStreamable& t)
{
	se_read(readBuffer, t.i);
	return true;
}
struct NonStreamable
{
	NonStreamable() = default;
	NonStreamable(int _i)
		: i(_i)
	{
	}
	int i = 0;
};

int main()
{
	se::CoreLib core;
	se::NetLib net(core);
	se::MathLib math(core);
	se::PhysicsLib physics(math);



	//se::TypelessPointer tp1;
	//tp1.reset(new Destructible(1));
	//se::TypelessPointer tp2(new int);
	//int* i = new int(3);
	//(void)i;


	se::log::info("");
	se::log::info("");
	se::log::info("");
	se::log::info("BEGIN testing nocommit");
	se::TypelessPointer tp1;
	se::WriteBuffer writeBuffer;
	tp1.reset(new MemberStreamable(1));
	tp1.write(writeBuffer);
	tp1.reset(new FreeStreamable(2));
	tp1.write(writeBuffer);
	tp1.reset(new NonStreamable(3));
	tp1.write(writeBuffer);
	tp1.reset(new int(6));
	tp1.write(writeBuffer);
	tp1.reset(new se::time::Time(7));
	tp1.write(writeBuffer);
	tp1.reset(new Destructible2(8));
	std::unique_ptr<Destructible2> unique(tp1.release<Destructible2>());

	se::ReadBuffer readBuffer(writeBuffer.getData(), writeBuffer.getSize());
	se::TypelessPointer tp2;
	tp2.read(readBuffer);
	tp2.read(readBuffer);
	tp2.read(readBuffer);
	tp2.read(readBuffer);
	tp2.read(readBuffer);


	return 0;
}
