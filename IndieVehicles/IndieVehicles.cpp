#include "plugin.h"
#include "CGeneral.h"
#include "../injector/assembly.hpp"
#include "CModelInfo.h"
#include "extensions/ScriptCommands.h"
#include "IndieVehHandlings/ExtendedHandling.h"

using namespace std;
using namespace plugin;
using namespace injector;

const int BUILD_NUMBER = 13;

class ExtraData {
public:
	struct
	{
		unsigned char bHandling : 1;
		unsigned char bCollModel : 1;
	} flags;

	float wheelFrontSize;
	float wheelRearSize;
	CColModel *colModel;

	ExtraData(CVehicle *vehicle) {
		flags.bHandling = false;
		flags.bCollModel = false;
		wheelFrontSize = (1.0f / 2);
		wheelRearSize = (1.0f / 2);
		colModel = nullptr;
	}
};
VehicleExtendedData<ExtraData> extraInfo;

fstream lg;
bool bTerminateIndieVehHandScript;

void asm_fmul(float f) {
	__asm {fmul f}
}
void WriteTiresToNewHandling(tExtendedHandlingData *handling, bool newTires);
void SetIndieNewHandling(CVehicle *vehicle, tHandlingData *originalHandling, bool newTires);


class IndieVehicles
{
public:

	IndieVehicles()
	{
		bTerminateIndieVehHandScript = true;

		lg.open("IndieVehicles.log", fstream::out | fstream::trunc);
		lg << "Build: " << BUILD_NUMBER << "\n\n";
		lg.flush();


		// CAEVehicleAudioEntity::Initialise (vehicle init, valid of all classes)
		MakeInline<0x004F7741>([](reg_pack &regs)
		{
			CVehicle *vehicle = (CVehicle *)regs.edx;
			ExtraData &xdata = extraInfo.Get(vehicle);
			if (!xdata.flags.bHandling)
			{
				if (vehicle->m_nModelIndex > 0)
				{
					SetIndieNewHandling(vehicle, vehicle->m_pHandlingData, false);
					xdata.flags.bHandling = true;
				}
			}
		});

		// Vehicle destructor
		Events::vehicleDtorEvent.before += [](CVehicle *vehicle)
		{
			ExtraData &xdata = extraInfo.Get(vehicle);
			if (xdata.flags.bHandling)
			{
				delete (tExtendedHandlingData*)vehicle->m_pHandlingData;
				xdata.flags.bHandling = false;
			}
		};


		///////////////////////////////////////////////


		// -- Wheel size

		// CAutomobile::SetupSuspensionLines
		MakeInline<0x006A66F4, 0x006A66F4 + 6>([](reg_pack& regs)
		{
			if (regs.ecx == 1 || regs.ecx == 3)
				asm_fmul(extraInfo.Get(reinterpret_cast<CVehicle*>(regs.edi)).wheelFrontSize);
			else
				asm_fmul(extraInfo.Get(reinterpret_cast<CVehicle*>(regs.edi)).wheelRearSize);
		});

		// CBike::SetupSuspensionLines
		MakeInline<0x006B8C3B, 0x006B8C3B + 6>([](reg_pack& regs)
		{
			if (regs.edx == 0 || regs.edx == 1)
				asm_fmul(extraInfo.Get(reinterpret_cast<CVehicle*>(regs.ebx)).wheelFrontSize);
			else
				asm_fmul(extraInfo.Get(reinterpret_cast<CVehicle*>(regs.ebx)).wheelRearSize);
		});


		// -- Coll model

		// CEntity::GetColModel
		MakeInline<0x00535300, 0x00535330>([](reg_pack& regs)
		{
			CEntity *entity = (CEntity *)regs.ecx;
			CColModel *colModel = nullptr;
			CBaseModelInfo *modelInfo = nullptr;

			if (entity->m_nType == eEntityType::ENTITY_TYPE_VEHICLE)
			{
				unsigned char defaultVehicleColID = reinterpret_cast<CVehicle*>(entity)->m_nSpecialColModel;
				if (defaultVehicleColID != 255)
				{
					colModel = &CVehicle::m_aSpecialColModel[defaultVehicleColID];
				}
				else
				{
					ExtraData &xdata = extraInfo.Get(reinterpret_cast<CVehicle*>(entity));
					if (&xdata != nullptr && xdata.flags.bCollModel)
					{
						colModel = xdata.colModel;
					}
					else
					{
						modelInfo = CModelInfo::GetModelInfo(entity->m_nModelIndex);
						if (modelInfo) colModel = modelInfo->m_pColModel;
					}
				}
			}
			else
			{
				modelInfo = CModelInfo::GetModelInfo(entity->m_nModelIndex);
				if (modelInfo) colModel = modelInfo->m_pColModel;
			}
			regs.eax = (uint32_t)colModel;
		});

		// CBike::SetupSuspensionLines
		MakeInline<0x006B89D4>([](reg_pack& regs)
		{
			CVehicle *vehicle = (CVehicle *)regs.ebx;
			CBaseModelInfo *modelInfo = CModelInfo::GetModelInfo(vehicle->m_nModelIndex);

			regs.esi = (uint32_t)modelInfo;

			CColModel *colModel;

			ExtraData &xdata = extraInfo.Get(vehicle);
			if (xdata.flags.bCollModel)
			{
				colModel = xdata.colModel;
			}
			else
			{
				colModel = modelInfo->m_pColModel;
			}
			*(uint32_t*)(regs.esp + 0x28) = (uint32_t)colModel;
			regs.edi = (uint32_t)colModel->m_pColData;
			regs.eax = (uint32_t)colModel->m_pColData->m_pLines;
			regs.edx = *(uint32_t*)(regs.eax + 0x28);

			regs.ecx = 0x47C34FFF; //mov ecx, 47C34FFFh
		});

		// CBike::SetupSuspensionLines - old version, incompatible with HOOK_FixBikeSuspLines from ModLoader
		/*
		MakeNOP(0x6B89B6, 4, true);
		MakeNOP(0x6B89BC, 10, true);
		MakeInline<0x006B89C7, 0x006B89C7 + 7>([](reg_pack& regs)
		{
			CVehicle *vehicle = (CVehicle *)regs.ebx;
			CBaseModelInfo *modelInfo = CModelInfo::GetModelInfo(vehicle->m_nModelIndex);

			regs.esi = (uint32_t)modelInfo;

			ExtraData &xdata = extraInfo.Get(vehicle);
			if (xdata.flags.bCollModel)
			{
				*(uint32_t*)(regs.esp + 0x28) = (uint32_t)xdata.colModel;
				regs.edi = (uint32_t)xdata.colModel->m_pColData;
			}
			else
			{
				*(uint32_t*)(regs.esp + 0x28) = (uint32_t)modelInfo->m_pColModel;
				regs.edi = (uint32_t)modelInfo->m_pColModel->m_pColData;
			}
		});
		*/

		///////////////////////////////////////////////

		Events::processScriptsEvent.after += []()
		{
			if (bTerminateIndieVehHandScript)
			{
				unsigned int script;
				Command<0x10AAA>("indiehn", &script); //GET_SCRIPT_STRUCT_NAMED
				if (script)
				{
					Command<0x10ABA>("indiehn"); //TERMINATE_ALL_CUSTOM_SCRIPTS_WITH_THIS_NAME
					WriteMemory<uint32_t>(0x6E2BB3, 0xFFEA50A9, true); //unhook stuff hooked by IndieVehHandlings.cs
					lg << "WARNING: You are using 'IndieVehHandlings.cs', it's useless now, delete it!!!\n";
					lg.flush();
				}
				bTerminateIndieVehHandScript = false;
			}
		};
		 
    }
} indieVehicles;


void SetIndieNewHandling(CVehicle *vehicle, tHandlingData *originalHandling, bool newTires)
{
	tExtendedHandlingData *newHandling = new tExtendedHandlingData;
	if (!newHandling)
	{
		lg << "ERROR: Unable to allocate new handling - " << (int)newHandling << "\n";
		lg.flush();
		MessageBoxA(0, "ERROR: Unable to allocate new handling. It's recommended not to continue playing, report the problem sending the IndieVehicles.log.", "IndieVehicles", 0);
		return;
	}
	if (!originalHandling)
	{
		lg << "ERROR: Original handling isn't valid - " << (int)originalHandling << " - model ID " << vehicle->m_nModelIndex << "\n";
		lg.flush();
		MessageBoxA(0, "ERROR: Original handling isn't valid. It's recommended not to continue playing, report the problem sending the IndieVehicles.log.", "IndieVehicles", 0);
		return;
	}
	memset(newHandling, 0, sizeof(tExtendedHandlingData));
	memmove(newHandling, originalHandling, sizeof(tHandlingData));
	WriteTiresToNewHandling(newHandling, newTires);
	vehicle->m_pHandlingData = (tHandlingData*)newHandling;
}


void WriteTiresToNewHandling(tExtendedHandlingData *handling, bool newTires)
{
	handling->tuningModFlags.bTire1 = true;
	if (newTires)
	{
		handling->fTireWear[0] = 255.0;
		handling->fTireWear[1] = 255.0;
		handling->fTireWear[2] = 255.0;
		handling->fTireWear[3] = 255.0;
	}
	else
	{
		float rand = CGeneral::GetRandomNumberInRange(160.0f, 240.0f); //base value
		handling->fTireWear[0] = rand; //FL
		handling->fTireWear[1] = (rand + CGeneral::GetRandomNumberInRange(-20.0f, 5.0f)); //RL
		handling->fTireWear[2] = (rand + CGeneral::GetRandomNumberInRange(-10.0f, 10.0f)); //FR
		handling->fTireWear[3] = (rand + CGeneral::GetRandomNumberInRange(-20.0f, 5.0f)); //RR
	}
}

CColModel * SetNewCol(CVehicle *vehicle)
{
	CColModel *newCol = new CColModel;
	if (newCol == nullptr)
	{
		lg << "ERROR: Unable to allocate new col model\n";
		lg.flush();
		MessageBoxA(0, "ERROR: Unable to allocate new col model. It's recommended not to continue playing, report the problem sending the IndieVehicles.log.", "IndieVehicles", 0);
		return nullptr;
	}

	newCol->AllocateData();
	if (newCol->m_pColData == nullptr)
	{
		lg << "ERROR: Unable to allocate new col data\n";
		lg.flush();
		MessageBoxA(0, "ERROR: Unable to allocate new col data. It's recommended not to continue playing, report the problem sending the IndieVehicles.log.", "IndieVehicles", 0);
		return nullptr;
	}

	CColModel *originalColModel = CModelInfo::GetModelInfo(vehicle->m_nModelIndex)->m_pColModel;
	*newCol = *originalColModel;
	return newCol;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" int32_t __declspec(dllexport) GetBuild()
{
	return BUILD_NUMBER;
}

extern "C" int32_t __declspec(dllexport) GetWheelSize(CVehicle *vehicle, int front)
{
	float size;
	ExtraData &xdata = extraInfo.Get(vehicle);
	if (front) size = xdata.wheelFrontSize;
	else size = xdata.wheelRearSize;
	return *reinterpret_cast<int32_t*>(std::addressof(size));
}

extern "C" void __declspec(dllexport) SetWheelSize(CVehicle *vehicle, int front, float size)
{
	ExtraData &xdata = extraInfo.Get(vehicle);
	if (!xdata.flags.bCollModel)
	{
		xdata.colModel = SetNewCol(vehicle);
		if (xdata.colModel) xdata.flags.bCollModel = true;
	}
	if (front) xdata.wheelFrontSize = size;
	else xdata.wheelRearSize = size;
	return;
}
 
extern "C" int32_t __declspec(dllexport) GetColl(CVehicle *vehicle)
{
	ExtraData &xdata = extraInfo.Get(vehicle);
	if (!xdata.flags.bCollModel)
	{
		xdata.colModel = SetNewCol(vehicle);
	}
	return (int32_t)xdata.colModel;
}

extern "C" void __declspec(dllexport) SetNewHandling(CVehicle *vehicle, tHandlingData *newOriginalHandling)
{
	ExtraData &xdata = extraInfo.Get(vehicle);
	if (xdata.flags.bHandling)
	{
		delete (tExtendedHandlingData*)vehicle->m_pHandlingData;
	}
	SetIndieNewHandling(vehicle, newOriginalHandling, true);
	xdata.flags.bHandling = true;
	return;
}

extern "C" void __declspec(dllexport) ignore() // it appears on crash logs, like modloader.log
{
	return;
}
