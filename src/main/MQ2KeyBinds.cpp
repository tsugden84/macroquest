/*
 * MacroQuest2: The extension platform for EverQuest
 * Copyright (C) 2002-2019 MacroQuest Authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "pch.h"
#include "MQ2Main.h"

#include <fmt/format.h>

namespace mq {

//void InjectMQ2Binds(COptionsWnd* pWnd);
//void EjectMQ2Binds(COptionsWnd* pWnd);

struct MQKeyBind
{
	std::string   Name;
	KeyCombo      Normal;
	KeyCombo      Alt;
	fMQExecuteCmd Function;
	bool          State = false;

	MQKeyBind(std::string name, fMQExecuteCmd func)
		: Name(std::move(name))
		, Function(func)
	{}
};

using KeybindMap = std::map<std::string, int, ci_less>;

std::vector<std::unique_ptr<MQKeyBind>> s_keybinds;
KeybindMap s_keybindMap;

static MQKeyBind* KeyBindByName(const char* name)
{
	auto iter = s_keybindMap.find(name);
	if (iter == std::end(s_keybindMap))
		return nullptr;

	return s_keybinds[iter->second].get();
}

static bool SetEQKeyBindByNumber(int index, bool alternate, KeyCombo& combo)
{
	if (index < nEQMappableCommands)
	{
		if (alternate)
			pKeypressHandler->AltKey[index] = combo;
		else
			pKeypressHandler->NormalKey[index] = combo;

		if (index < nNormalEQMappableCommands)
			pKeypressHandler->SaveKeymapping(index, combo, alternate);

		return true;
	}

	return false;
}

static bool SetEQKeyBind(const char* name, bool alternate, KeyCombo& combo)
{
	return SetEQKeyBindByNumber(FindMappableCommand(name), alternate, combo);
}

bool MQ2HandleKeyDown(const KeyCombo& combo)
{
	bool Ret = false;

	for (int index = 0; index < nEQMappableCommands; index++)
	{
		if (pKeypressHandler->CommandState[index] == 0
			&& (pKeypressHandler->NormalKey[index] == combo || pKeypressHandler->AltKey[index] == combo))
		{
			ExecuteCmd(index, true);

			pKeypressHandler->CommandState[index] = 1;
			Ret = true;
		}
	}

	for (auto& pKeybind : s_keybinds)
	{
		if (pKeybind
			&& pKeybind->State == 0
			&& (pKeybind->Normal == combo || pKeybind->Alt == combo))
		{
			pKeybind->Function(pKeybind->Name.c_str(), true);
			pKeybind->State = true;

			Ret = true;
		}
	}

	return Ret;
}

bool MQ2HandleKeyUp(const KeyCombo& combo)
{
	bool Ret = false;

	for (int index = 0; index < nEQMappableCommands; index++)
	{
		if (pKeypressHandler->CommandState[index]
			&& (pKeypressHandler->NormalKey[index].Data[3] == combo.Data[3] || pKeypressHandler->AltKey[index].Data[3] == combo.Data[3]))
		{
			ExecuteCmd(index, false);

			pKeypressHandler->CommandState[index] = 0;
			Ret = true;
		}
	}

	for (auto& pKeybind : s_keybinds)
	{
		if (pKeybind
			&& pKeybind->State == 1
			&& (pKeybind->Normal.Data[3] == combo.Data[3] || pKeybind->Alt.Data[3] == combo.Data[3]))
		{
			pKeybind->Function(pKeybind->Name.c_str(), false);
			pKeybind->State = false;

			Ret = true;
		}
	}

	return Ret;
}

class KeypressHandlerHook
{
public:
	void ClearCommandStateArray_Hook()
	{
		for (auto& pKeybind : s_keybinds)
		{
			if (pKeybind)
			{
				pKeybind->State = false;
			}
		}

		ZeroMemory(&pKeypressHandler->CommandState[0], sizeof(pKeypressHandler->CommandState));
	}
	void ClearCommandStateArray_Trampoline();

	bool HandleKeyDown_Hook(const KeyCombo& combo)
	{
		if (!pWndMgr->HandleKeyboardMsg(combo.Data[3], true))
			return true;

		return (MQ2HandleKeyDown(combo) != 0);
	}
	bool HandleKeyDown_Trampoline(const KeyCombo&);

	bool HandleKeyUp_Hook(const KeyCombo& combo)
	{
		bool ret = false;
		if (!pWndMgr->HandleKeyboardMsg(combo.Data[3], false))
			ret = true;

		return MQ2HandleKeyUp(combo) || ret;
	}
	bool HandleKeyUp_Trampoline(const KeyCombo&);
};

DETOUR_TRAMPOLINE_EMPTY(void KeypressHandlerHook::ClearCommandStateArray_Trampoline());
DETOUR_TRAMPOLINE_EMPTY(bool KeypressHandlerHook::HandleKeyDown_Trampoline(const KeyCombo&));
DETOUR_TRAMPOLINE_EMPTY(bool KeypressHandlerHook::HandleKeyUp_Trampoline(const KeyCombo&));

static void DoRangedBind(const char* Name, bool Down);

void InitializeMQ2KeyBinds()
{
	AddMQ2KeyBind("RANGED", DoRangedBind);

	EzDetour(KeypressHandler__ClearCommandStateArray, &KeypressHandlerHook::ClearCommandStateArray_Hook, &KeypressHandlerHook::ClearCommandStateArray_Trampoline);
	EzDetour(KeypressHandler__HandleKeyDown, &KeypressHandlerHook::HandleKeyDown_Hook, &KeypressHandlerHook::HandleKeyDown_Trampoline);
	EzDetour(KeypressHandler__HandleKeyUp, &KeypressHandlerHook::HandleKeyUp_Hook, &KeypressHandlerHook::HandleKeyUp_Trampoline);
}

void ShutdownMQ2KeyBinds()
{
	s_keybinds.clear();
	s_keybindMap.clear();

	RemoveDetour(KeypressHandler__ClearCommandStateArray);
	RemoveDetour(KeypressHandler__HandleKeyDown);
	RemoveDetour(KeypressHandler__HandleKeyUp);
}

bool AddMQ2KeyBind(const char* name, fMQExecuteCmd function)
{
	DebugSpew("AddMQ2KeyBind(%s)", name);

	if (KeyBindByName(name))
	{
		DebugSpew("AddMQ2KeyBind(%s) - Name already in use", name);
		return false;
	}

	auto pKeybind = std::make_unique<MQKeyBind>(name, function);

	std::string primarySettingName = fmt::format("{}_Nrm", pKeybind->Name);
	std::string primaryBind = GetPrivateProfileString("Key Binds", primarySettingName, "clear", mq::internal_paths::MQini);

	std::string altSettingName = fmt::format("{}_Alt", pKeybind->Name);
	std::string altBind = GetPrivateProfileString("Key Binds", altSettingName, "clear", mq::internal_paths::MQini);

	ParseKeyCombo(primaryBind.c_str(), pKeybind->Normal);
	ParseKeyCombo(altBind.c_str(), pKeybind->Alt);

	// Find an unused index.
	int index = -1;
	for (size_t i = 0; i < s_keybinds.size(); ++i)
	{
		if (s_keybinds[i] == nullptr)
		{
			index = i;
			break;
		}
	}

	if (index == -1)
	{
		s_keybinds.emplace_back();
		index = s_keybinds.size() - 1;
	}

	s_keybinds[index] = std::move(pKeybind);
	s_keybindMap.insert_or_assign(name, index);

	return true;
}

bool GetMQ2KeyBind(const char* name, bool alt, KeyCombo& combo)
{
	if (MQKeyBind* pKeybind = KeyBindByName(name))
	{
		combo = alt ? pKeybind->Alt : pKeybind->Normal;
		return true;
	}
	return false;
}

bool RemoveMQ2KeyBind(const char* name)
{
	DebugSpew("RemoveMQ2KeyBind(%s)", name);

	auto iter = s_keybindMap.find(name);
	if (iter == std::end(s_keybindMap))
		return false;

	s_keybinds[iter->second].reset();
	s_keybindMap.erase(iter);

	return true;
}

bool PressMQ2KeyBind(const char* name, bool hold)
{
	if (MQKeyBind* pKeybind = KeyBindByName(name))
	{
		pKeybind->Function(pKeybind->Name.c_str(), true);

		// release the keypress if we aren't asked to hold it.
		if (!hold)
		{
			pKeybind->Function(pKeybind->Name.c_str(), false);
		}

		return true;
	}

	return false;
}

bool SetMQ2KeyBind(const char* name, bool alternate, KeyCombo& combo)
{
	if (MQKeyBind* pKeybind = KeyBindByName(name))
	{
		std::string settingName;

		if (!alternate)
		{
			settingName = fmt::format("{}_Nrm", pKeybind->Name);
			pKeybind->Normal = combo;
		}
		else
		{
			settingName = fmt::format("{}_Alt", pKeybind->Name);
			pKeybind->Alt = combo;
		}

		char szBuffer[MAX_STRING] = { 0 };

		WritePrivateProfileString("Key Binds", settingName,
			DescribeKeyCombo(combo, szBuffer, sizeof(szBuffer)), mq::internal_paths::MQini);
		return true;
	}

	return false;
}

int FindMappableCommand(const char* name)
{
	for (int i = 0; i < nEQMappableCommands; i++)
	{
		if (szEQMappableCommands[i] == nullptr || szEQMappableCommands[i] > reinterpret_cast<const char*>(g_eqgameimagesize))
			continue;

		if (!_stricmp(name, szEQMappableCommands[i]))
			return i;
	}

	return -1;
}

void MQ2KeyBindCommand(SPAWNINFO* pChar, char* szLine)
{
	if (szLine[0] == 0)
	{
		WriteChatColor("Usage: /bind <list|eqlist|[~]name <combo|clear>>");
		return;
	}

	char szArg1[MAX_STRING] = { 0 };
	GetArg(szArg1, szLine, 1);
	char* szRest = GetNextArg(szLine);
	char* szArg = &szArg1[0];

	bool altKey = false;

	if (szArg[0] == '~')
	{
		altKey = true;
		szArg = &szArg1[1];
	}

	if (!_stricmp(szArg, "list"))
	{
		// list binds
		char szNormal[MAX_STRING] = { 0 };
		char szAlt[MAX_STRING] = { 0 };
		WriteChatColor("MQ2 Binds");
		WriteChatColor("--------------");

		for (auto& pKeybind : s_keybinds)
		{
			if (pKeybind)
			{
				WriteChatf("[\ay%s\ax] Nrm:\at%s\ax Alt:\at%s\ax",
					pKeybind->Name.c_str(), DescribeKeyCombo(pKeybind->Normal, szNormal, sizeof(szNormal)),
					DescribeKeyCombo(pKeybind->Alt, szAlt, sizeof(szAlt)));
			}
		}

		WriteChatColor("--------------");
		WriteChatColor("End MQ2 Binds");
		return;
	}

	if (!_stricmp(szArg, "eqlist"))
	{
		char szNormal[MAX_STRING] = { 0 };
		char szAlt[MAX_STRING] = { 0 };

		// list eq binds
		WriteChatColor("EQ Binds");
		WriteChatColor("--------------");

		//ok first of all, the 0x11111111 constant for ac1 is not even used anymore they stopped using those ones years ago
		//its just left in there, legacy code, I suppose it would cause more problems removing it, than leaving it, and if a eq dev sees this, please
		//just leave that stuff alone because i dont want to have to worry about where it went or if its active again. -eqmule
		//second, even though its not active, now that eqgame uses aslr, we have to take that into account and actually add load address to the constant, thus the line below:

		//anyway... from now on we will just use the size of the image... not reliable to have it hardcoded
		for (int i = 0; i < nEQMappableCommands; i++)
		{
			if (szEQMappableCommands[i] == nullptr || szEQMappableCommands[i] > reinterpret_cast<const char*>(g_eqgameimagesize))
				continue;

			WriteChatf("[\ay%s\ax] Nrm:\at%s\ax Alt:\at%s\ax", szEQMappableCommands[i],
				DescribeKeyCombo(pKeypressHandler->NormalKey[i], szNormal, sizeof(szNormal)),
				DescribeKeyCombo(pKeypressHandler->AltKey[i], szAlt, sizeof(szAlt)));
		}

		WriteChatColor("--------------");
		WriteChatColor("End EQ Binds");
		return;
	}

	KeyCombo newCombo;

	if (!ParseKeyCombo(szRest, newCombo))
	{
		WriteChatColor("Invalid key combination");
		return;
	}

	if (!_stricmp(szArg, "clearcombo"))
	{
		KeyCombo ClearCombo;

		// mq2 binds
		for (auto& pKeybind : s_keybinds)
		{
			if (pKeybind)
			{
				if (pKeybind->Alt == newCombo && SetMQ2KeyBind(pKeybind->Name.c_str(), true, ClearCombo))
				{
					WriteChatf("Alternate %s cleared", pKeybind->Name.c_str());
				}

				if (pKeybind->Normal == newCombo && SetMQ2KeyBind(pKeybind->Name.c_str(), false, ClearCombo))
				{
					WriteChatf("Normal %s cleared", pKeybind->Name.c_str());
				}
			}
		}

		// eq binds
		for (int i = 0; i < nEQMappableCommands; i++)
		{
			if (szEQMappableCommands[i] == nullptr || szEQMappableCommands[i] > reinterpret_cast<const char*>(g_eqgameimagesize))
				continue;

			if (pKeypressHandler->AltKey[i] == newCombo && SetEQKeyBindByNumber(i, true, ClearCombo))
			{
				WriteChatf( "Alternate %s cleared", szEQMappableCommands[i]);
			}
			if (pKeypressHandler->NormalKey[i] == newCombo && SetEQKeyBindByNumber(i, false, ClearCombo))
			{
				WriteChatf("Normal %s cleared", szEQMappableCommands[i]);
			}
		}

		return;
	}

	char szBuffer[MAX_STRING] = { 0 };
	if (SetMQ2KeyBind(szArg, altKey, newCombo))
	{
		MQKeyBind* pKeybind = KeyBindByName(szArg);

		WriteChatf("%s %s now bound as %s",
			altKey ? "Alternate" : "Normal",
			pKeybind->Name.c_str(),
			DescribeKeyCombo(newCombo, szBuffer, sizeof(szBuffer)));
		return;
	}

	int index = FindMappableCommand(szArg);
	if (index < 0)
	{
		WriteChatColor("Unknown bind command name");
		return;
	}

	if (index > nNormalEQMappableCommands)
	{
		WriteChatColor("Unmappable bind command name");
		return;
	}

	if (SetEQKeyBindByNumber(index, altKey, newCombo))
	{
		WriteChatf("%s %s now bound as %s",
			altKey ? "Alternate" : "Normal",
			szEQMappableCommands[index],
			DescribeKeyCombo(altKey ? pKeypressHandler->AltKey[index] : pKeypressHandler->NormalKey[index], szBuffer, sizeof(szBuffer)));
	}
}

static void DoRangedBind(const char* Name, bool Down)
{
	if (Down && pTarget && gbRangedAttackReady)
	{
		pLocalPlayer->DoAttack(0x0B, 0, pTarget);
		gbRangedAttackReady = 0;
	}
}

bool DumpBinds(const char* Filename)
{
	std::filesystem::path pathFilename = Filename;
	if (!strchr(Filename, '.'))
		pathFilename = std::string(Filename) + ".cfg";

	if (pathFilename.is_relative())
	{
		pathFilename = mq::internal_paths::Config / pathFilename;
	}

	FILE* file = nullptr;
	errno_t err = fopen_s(&file, pathFilename.string().c_str(), "wt");
	if (err)
	{
		return false;
	}

	char szBuffer[MAX_STRING] = { 0 };

	for (int index = 0; index < nEQMappableCommands; index++)
	{
		if (szEQMappableCommands[index] == nullptr || szEQMappableCommands[index] > reinterpret_cast<const char*>(g_eqgameimagesize))
			continue;

		fprintf(file, "/bind %s %s\n", szEQMappableCommands[index],
			DescribeKeyCombo(pKeypressHandler->NormalKey[index], szBuffer, sizeof(szBuffer)));
		fprintf(file, "/bind ~%s %s\n", szEQMappableCommands[index],
			DescribeKeyCombo(pKeypressHandler->AltKey[index], szBuffer, sizeof(szBuffer)));
	}

	for (auto& pKeybind : s_keybinds)
	{
		if (pKeybind)
		{
			fprintf(file, "/bind %s %s\n", pKeybind->Name.c_str(),
				DescribeKeyCombo(pKeybind->Normal, szBuffer, sizeof(szBuffer)));
			fprintf(file, "/bind ~%s %s\n", pKeybind->Name.c_str(),
				DescribeKeyCombo(pKeybind->Alt, szBuffer, sizeof(szBuffer)));
		}
	}

	fclose(file);
	return true;
}

} // namespace mq
