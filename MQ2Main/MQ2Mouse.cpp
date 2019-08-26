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

#include "MQ2Main.h"

void MouseButtonUp(DWORD x, DWORD y, char* szButton);

// ***************************************************************************
// EqMule Mar 08 2014
// Adding a detour here
// All it does is check if EnviroTarget.StandState==STANDSTATE_STAND
// if it is, we know the user issued a /click left item
// so we force the return of the correct switch...
// That will have the effect of actually "clicking" it...
// ***************************************************************************
class FakeCDisplay
{
public:
	CActorInterface* GetClickedActor_Tramp(int, int, bool, CVector3&, CVector3&);
	CActorInterface* GetClickedActor_Detour(int X, int Y, bool bFlag, CVector3& Vector1, CVector3& Vector2)
	{
		if (GroundObject.Type == GO_ObjectType)
		{
			if (EQPlacedItem*pPlaced = (EQPlacedItem*)GroundObject.ObjPtr)
			{
				CActorInterface* ret = (CActorInterface*)pPlaced->pActor;
				return ret;
			}
			else
			{
				CActorInterface* ret = (CActorInterface*)GetClickedActor_Tramp(X,Y,bFlag,Vector1,Vector2);
				return ret;
			}
		} else if(pGroundTarget && EnviroTarget.Name[0] && (EnviroTarget.StandState==STANDSTATE_STAND || EnviroTarget.StandState==STANDSTATE_SIT)) {
			//we do this to take both mousedown and mouseup into account
			if(EnviroTarget.StandState==STANDSTATE_STAND) {
				EnviroTarget.StandState=STANDSTATE_SIT;
			} else if(EnviroTarget.StandState==STANDSTATE_SIT) {
				EnviroTarget.StandState=STANDSTATE_DEAD;
			}
			//T3D_tagACTORINSTANCE* org = GetClickedActor_Tramp(558,580,bFlag,Vector1,Vector2);
			CActorInterface* ret = (CActorInterface*)pGroundTarget->pSwitch;
			return ret;
		} else {
			CActorInterface* ret = GetClickedActor_Tramp(X,Y,bFlag,Vector1,Vector2);
			if (PEQSWITCH sw = (PEQSWITCH)ret)
			{
				EQPlacedItem *pi = (EQPlacedItem*)sw->pActorApplicationData;
				Sleep(0);
			}
			return ret;
		}
	}

	HRESULT GetViewport(void* This, void* pViewport);
	HRESULT GetTransform(void* This, DWORD State, void* pMatrix);
	void SetCursorPosition(void* This, int X, int Y, DWORD Flags);//0x2c
	BOOL ShowCursor(void* This, BOOL bShow); // 0x30

/*0x000*/ BYTE Unknown0x0[0xec8];
/*0xec8*/ void* pDevice; // device pointer see 100019B4                 mov     ecx, [ecx+0F08h] in 2015 02 20
};

DETOUR_TRAMPOLINE_EMPTY(CActorInterface* FakeCDisplay::GetClickedActor_Tramp(int X, int Y, bool bFlag, CVector3& Vector1, CVector3& Vector2));

void MQ2MouseHooks(BOOL bFlag)
{
	if (bFlag)
	{
		EzDetourwName(CDisplay__GetClickedActor, &FakeCDisplay::GetClickedActor_Detour, &FakeCDisplay::GetClickedActor_Tramp, "CDisplay__GetClickedActor");
	}
	else {
		RemoveDetour(CDisplay__GetClickedActor);
	}
}

// ***************************************************************************
// Function: ParseMouseLoc
// Description: Parses mouseloc for /click and /mouseto
// ***************************************************************************
//Function used by ParseLocationXML to extract parameter
// ExtractValue - gets value between specified start and end markers
// - Parameters:
// szFile - pointer to string to look in
// szStart - pointer to string to mark start of value
// szEnd - pointer to string to mark end of value
// szValue - pointer to string to contain the value found
// - Return Value:
// if successful, return TRUE, szValue contains the value between the start and end markers
// if unsuccessful, return FALSE
BOOL ExtractValue(char* szFile, char* szStart, char* szEnd, char* szValue)
{
    // verify we have legal pointers passed to us
    if (!szValue)
        return FALSE;

    if (!szFile || !szStart || !szEnd)
    {
        szValue[0] = 0;
        return FALSE;
    }

    DWORD lenStart = strlen(szStart);
    char* fence = strstr(szFile,"ScreenID"); // needed to make sure we don't start into another element

    char* sub = strstr(szFile, szStart);
    char* sub2 = strstr(szFile, szEnd);
    if (!sub || !sub2 || (fence && (sub > fence))) {
        szValue[0] = 0;
        return FALSE;
    }

    memcpy(szValue,sub + lenStart, (sub2-sub) - lenStart);
    szValue[(sub2-sub) - lenStart] = 0;
    DebugSpew("Value extracted for %s was %s",szStart,szValue);
    return TRUE;
}

bool MoveMouse(int x, int y, bool bClick)
{
	if (!EQADDR_MOUSE)
		return false;

	HWND EQhWnd = *(HWND*)EQADDR_HWND;
	if (!EQhWnd)
		return false;

	CXPoint pt{ x, y };

	ClientToScreen(EQhWnd, (LPPOINT)&pt);
	SetCursorPos(pt.x, pt.y);

	EQADDR_DIMOUSECOPY->y = y;
	EQADDR_DIMOUSECOPY->x = x;
	EQADDR_MOUSE->Y = EQADDR_DIMOUSECOPY->y;
	EQADDR_DIMOUSECHECK->y = EQADDR_DIMOUSECOPY->y;
	EQADDR_MOUSE->X = EQADDR_DIMOUSECOPY->x;
	EQADDR_DIMOUSECHECK->x = EQADDR_DIMOUSECOPY->x;

	pWndMgr->MousePoint = pt;
	pWndMgr->StoredMousePos = pt;

	if (bClick)
	{
		MouseButtonUp(x, y, "left");
	}

	WeDidStuff();
	DebugSpew("Moved mouse to: %d,%d", x, y);
	return true;
}

BOOL ParseMouseLoc(CHARINFO* pCharInfo, char* szMouseLoc)
{
	char szArg1[MAX_STRING] = {0};
	char szArg2[MAX_STRING] = {0};
	int ClickX; //actual location to click, calculated from ButtonX
	int ClickY; //actual location to click, calculated from ButtonY
	if (!_strnicmp(szMouseLoc, "target", 6)) {
        if (!pTarget) {
            WriteChatColor("You must have a target selected for /mouseto target.",CONCOLOR_RED);
            return FALSE;
        }
		//insert code here to move mouse to target
		//work in progress -eqmule july 18 2015
		//this is comming in next zip, they ninja patched on me so im
		//gonna have to add this like tomorrow...
		WeDidStuff();
		return TRUE;
    }
	// determine mouse location - x and y given
	if ((szMouseLoc[0]=='+') || (szMouseLoc[0]=='-') || ((szMouseLoc[0]>='0') && (szMouseLoc[0]<='9')))
	{ // x and y were given so lets convert them and move mouse
		GetArg(szArg1,szMouseLoc,1);
		GetArg(szArg2,szMouseLoc,2);
		ClickX = atoi(szArg1);
		ClickY = atoi(szArg2);
		if ((szArg1[0]=='+') || (szArg1[0]=='-') || (szArg2[0]=='+') || (szArg2[0]=='-'))
		{ // relative location was passed so offset from current
			ClickX += EQADDR_MOUSE->X;
			ClickY += EQADDR_MOUSE->Y;
			DebugSpew("Moving mouse by relative offset");
		} else {
			DebugSpew("Moving mouse to absolute position");
		}
		return MoveMouse(ClickX,ClickY);
	}
	MacroError("'%s' mouse click is either invalid or should be done using /notify",szMouseLoc);
	return FALSE;
}
// TODO: Expand this to support mouse buttons 3->8
void ClickMouse(DWORD button)
{
   if (button > 1)
      return;
   DWORD mdType = (DWORD)MD_Button0 + button;

   if (!((EQADDR_MOUSECLICK->Click[button] == 0x80) && (!EQADDR_MOUSECLICK->Confirm[button])))
      EQADDR_MOUSECLICK->Click[button] = 0x80;

   gMouseClickInProgress[button] = TRUE;

   WeDidStuff();
   if(PMOUSESPOOF pData = (PMOUSESPOOF)malloc(sizeof(MOUSESPOOF))) {
	   pData->mdType = (MOUSE_DATA_TYPES)mdType;
	   pData->dwData = 0x00;
	   pData->pNext = NULL;
	   if (!gMouseData) {
		  gMouseData = pData;
	   } else {
		  PMOUSESPOOF pTemp = gMouseData;
		  while (pTemp->pNext) {
			 pTemp = pTemp->pNext;
		  }
		  pTemp->pNext = pData;
	   }
   }
}

void MouseButtonUp(DWORD x, DWORD y, char* szButton)
{
	CVector3 cv1 = { 0,0,0 };
	CVector3 cv2 = { 0,0,0 };
    gLClickedObject=false;

    if(!_strnicmp(szButton,"left",4))
    {
        // click will fail if this isn't set to a time less than TimeStamp minus 750ms
        *((DWORD*)__LMouseHeldTime)=((CDISPLAY*)pDisplay)->TimeStamp-0x45;
        pEverQuest->LMouseUp(x,y);

        if(((CDisplay*)pDisplay)->GetClickedActor(x,y,0,cv1,cv2))
        {
            gLClickedObject=true;
            EnviroTarget.Name[0]=0;
        }
    }

    /* i don't think there's any use for this currently - ieatacid
    else if(!_strnicmp(szButton,"right",5))
    {
        // click will fail if this isn't set to a time less than TimeStamp minus 500ms
        *((DWORD*)__RMouseHeldTime)=((PCDISPLAY)pDisplay)->TimeStamp-0x45;
        pEverQuest->RMouseUp(x,y);
    }*/
}

void ClickMouseLoc(char* szMouseLoc, char* szButton)
{
    char szArg1[MAX_STRING] = {0};
    char szArg2[MAX_STRING] = {0};
    int ClickX; //actual location to click, calculated from ButtonX
    int ClickY; //actual location to click, calculated from ButtonY

    // determine mouse location - x and y given
    if ((szMouseLoc[0]=='+') || (szMouseLoc[0]=='-') || ((szMouseLoc[0]>='0') && (szMouseLoc[0]<='9')))
    { // x and y were given so lets convert them and move mouse
        GetArg(szArg1,szMouseLoc,1);
        GetArg(szArg2,szMouseLoc,2);
        ClickX = atoi(szArg1);
        ClickY = atoi(szArg2);
        if ((szArg1[0]=='+') || (szArg1[0]=='-') || (szArg2[0]=='+') || (szArg2[0]=='-'))
        { // relative location was passed so offset from current
            ClickX += EQADDR_MOUSE->X;
            ClickY += EQADDR_MOUSE->Y;
            DebugSpew("Clicking mouse by relative offset");
        } else {
            DebugSpew("Clicking mouse at absolute position");
        }
        MouseButtonUp(ClickX,ClickY,szButton);
    }
    else
    {
        MacroError("'%s' mouse click is either invalid or should be done using /notify",szMouseLoc);
    }
}

// ***************************************************************************
// Function: Click
// Description: Our '/click' command
// Clicks the mouse button (calls EQ's mouse up commands)
// Usage: /click left|right [<mouseloc>]
// ***************************************************************************
BOOL IsMouseWaitingForButton()
{
    return ((EQADDR_MOUSECLICK->Click[1] == EQADDR_MOUSECLICK->Confirm[1])
        && (EQADDR_MOUSECLICK->Click[0] == EQADDR_MOUSECLICK->Confirm[0])) ? FALSE : TRUE;
}

BOOL IsMouseWaiting()
{
    BOOL Result = FALSE;

    if (IsMouseWaitingForButton()) Result = TRUE;
    else {
        // Here we basically force the MQ script engine to wait for the button up on mouse clicks

        if (gMouseClickInProgress[0]) {
            //SetMouseButtonUpL();
            if (!((!EQADDR_MOUSECLICK->Click[0]) && (EQADDR_MOUSECLICK->Confirm[0] == 0x80))) EQADDR_MOUSECLICK->Click[0] = 0x0;
            gMouseClickInProgress[0] = FALSE;
            Result = TRUE;
        }
        if (gMouseClickInProgress[1]) {
            //SetMouseButtonUpR();
            if (!((!EQADDR_MOUSECLICK->Click[1]) && (EQADDR_MOUSECLICK->Confirm[1] == 0x80))) EQADDR_MOUSECLICK->Click[1] = 0x0;
            gMouseClickInProgress[1] = FALSE;
            Result = TRUE;
        }
    }
    return Result;
}

void Click(PSPAWNINFO pChar, char* szLine)
{
	if(GetGameState()!=GAMESTATE_INGAME) {
		WriteChatf("Dont /click stuff(%s) when not in game... Gamestate is %d",szLine,GetGameState());
		return;
	}
	if (gZoning) return;
	char szMouseLoc[MAX_STRING] = { 0 };
    MOUSE_DATA_TYPES mdType = MD_Unknown;
    DWORD RightOrLeft = 0; 
	char szArg1[MAX_STRING] = { 0 };
    GetArg(szArg1, szLine, 1); //left or right
    strcpy_s(szMouseLoc,GetNextArg(szLine, 1)); //location to click

    //parse location for click location (szMouseLoc) here
    if (szMouseLoc && szMouseLoc[0]!=0) {
        if (!_strnicmp(szMouseLoc, "target", 6)) {
            if (!pTarget) {
                WriteChatColor("You must have a target selected for /click x target.",CONCOLOR_RED);
				return;
            }
            if (!_strnicmp(szArg1, "left", 4)) { 
                pEverQuest->LeftClickedOnPlayer(pTarget);
                WeDidStuff();
            } else if (!_strnicmp(szArg1, "right", 5)) {
                pEverQuest->RightClickedOnPlayer(pTarget, 0);
                WeDidStuff();
            }
			return;
        }
		else if(!_strnicmp(szMouseLoc,"center",6)) {
            sprintf_s(szMouseLoc,"%d %d",ScreenXMax/2,ScreenYMax/2);
        }
		else if (!_strnicmp(szMouseLoc, "item", 4)) {
			if(GroundObject.Type!=GO_None) {
				if (!_strnicmp(szArg1, "right", 4)) {
					if (EnviroTarget.Name[0] != 0) {
						if (Distance3DToSpawn(pChar, &EnviroTarget) <= 20.0f) {
							*((DWORD*)__RMouseHeldTime) = ((CDISPLAY*)pDisplay)->TimeStamp - 0x45;
							if (pWndMgr)
							{
								pEverQuest->RMouseUp(pWndMgr->MousePoint.x, pWndMgr->MousePoint.y);
							}
							ZeroMemory(&EnviroTarget, sizeof(EnviroTarget));
							ZeroMemory(&GroundObject, sizeof(GroundObject));
							pGroundTarget = NULL;
						}
						else {
							WriteChatf("You are to far away from the item, please move closer before issuing the /click right item command.");
						}
					}
					else {
						WriteChatf("No item targeted, use /itemtarget <theid> before issuing a /click right item command.");
					}
				}
				else if (!_strnicmp(szArg1, "left", 4)) {
					if(EnviroTarget.Name[0]!=0) {
						if(Distance3DToSpawn(pChar,&EnviroTarget)<=20.0f) {
							//do stuff
							*((DWORD*)__LMouseHeldTime)=((CDISPLAY*)pDisplay)->TimeStamp-0x45;
							//we "click" at -1000,-1000 because we know the user doesnt have any windows there...
							//if its possible, i would like to figure out a pixel
							//on the users screen that isnt covered by a window...
							//the click need to be issued on the main UI...
							//but for now this will work -eqmule 8 mar 2014
							pEverQuest->LMouseUp(-10000, -10000);
							ZeroMemory(&EnviroTarget, sizeof(EnviroTarget));
							pGroundTarget = NULL;
							ZeroMemory(&GroundObject, sizeof(GroundObject));
						} else {
							WriteChatf("You are to far away from the item, please move closer before issuing the /click left item command.");
						}
					} else {
						WriteChatf("No Item targeted, use /itemtarget <theid> before issuing a /click left item command.");
					}
				} else {
					WriteChatf("Invalid click args, use \"/click left item\", aborting: %s",szMouseLoc);
				}
			} else {
				WriteChatf("No Item targeted, use /itemtarget <theid> before issuing a /click left|right item command.");
			}
			return;
        } else if (!_strnicmp(szMouseLoc, "door", 4)) {
			// a right clicked door spawn does nothing
			if(pDoorTarget) {
				if (!_strnicmp(szArg1, "left", 4)) {
					if(DoorEnviroTarget.Name[0]!=0) {
						//the distance needs to be calculated by the outer radius of the door and the characters reach...
						float BoundingRadius = 0;
						if (ActorBase*pBase = (ActorBase*)pDoorTarget->pSwitch) {
							BoundingRadius = pBase->GetBoundingRadius();
						} else {
							BoundingRadius = pDoorTarget->ScaleFactor * 0.01f;
						}
						float Dist = pDisplay->TrueDistance(((SPAWNINFO*)pCharSpawn)->Y, ((SPAWNINFO*)pCharSpawn)->X, ((SPAWNINFO*)pCharSpawn)->Z, pDoorTarget->Y, pDoorTarget->X, pDoorTarget->Z, 0.0f);
						float reach = ((SPAWNINFO*)pCharSpawn)->Height + 20.0f + BoundingRadius;
						if(Dist <= reach) {
							int KeyID = 0;
							int Skill = 0;
							if (CHARINFO2* pChar2 = GetCharInfo2()) {
								if (pChar2->pInventoryArray && pChar2->pInventoryArray->Inventory.Cursor)
								{
									if (PITEMINFO pItem = GetItemFromContents(pChar2->pInventoryArray->Inventory.Cursor)) {
										switch (pItem->ItemType)
										{
										case 33://Key
											KeyID = pItem->ItemNumber;
											Skill = 0;
											break;
										case 11://Misc (some keys have this type)
										case 12://Lockpicks
											KeyID = pItem->ItemNumber;
											Skill = GetAdjustedSkill(SKILL_PICKLOCK);
											break;
										default:
											KeyID = pItem->ItemNumber;
											Skill = 0;
											break;
										}
									}
								}
							}

							((EQSwitch*)pDoorTarget)->UseSwitch(pChar->SpawnID, KeyID, Skill);
							if (pTarget == (PlayerClient*)& DoorEnviroTarget) {//this should NEVER happen
								pTarget = nullptr;
							}
							return;
						} else {
							WriteChatf("You are to far away from the door, please move closer before issuing the /click left door command.");
						}
					} else {
						WriteChatf("No Door targeted, use /doortarget <theid> before issuing a /click left door command.");
					}
				} else {
					WriteChatf("Invalid click args, use \"/click left door\", aborting: %s",szMouseLoc);
				}
			} else {
				WriteChatf("No Door targeted, use /doortarget <theid> before issuing a /click left door command.");
			}
			return;
		}
        ClickMouseLoc(szMouseLoc, szArg1);
		return;
    }

    if (szArg1[0]!=0) {
        if (!_strnicmp(szArg1, "left", 4)) {
            ClickMouse(0);
        } else if (!_strnicmp(szArg1, "right", 5)) {
            ClickMouse(1);
        } else {
            WriteChatColor("Usage: /click <left|right>",USERCOLOR_DEFAULT);
            DebugSpew("Bad command: %s",szLine);
			return;
        }
    }
}
// ***************************************************************************
// Function: MouseTo
// Description: Our '/mouseto' command
// Moves the mouse
// Usage: /mouseto <mouseloc>
// ***************************************************************************
void MouseTo(SPAWNINFO* pChar, char* szLine)
{
	if (szLine && szLine[0]) {
		if (ParseMouseLoc(GetCharInfo(), szLine)) {
			return;
		}
	}

	WriteChatColor("Usage: /mouseto <mouseloc>",USERCOLOR_DEFAULT); 
	DebugSpew("Help invoked or Bad MouseTo command: %s",szLine); 
}

//ok here goes... this is our custom directx implementation to allow us to
//mess with directx functions without having to include d3d9.lib or any d3d9 headers
//it was just easier than adding another dependency to the project and i dont like dependencies -eqmule
/******************SUPA DUPA CODE AND CUSTOM DIRECTX STUFF BELOW*****************/
//first some defines
#define EQD3DTS_VIEW 2
#define EQD3DTS_PROJECTION 3
#define EQD3DTS_WORLD  256


//Jul 18 2015 -eqmule
typedef struct _EqViewPort9 {
    DWORD       X;
    DWORD       Y;            /* Viewport Top left */
    DWORD       Width;
    DWORD       Height;       /* Viewport Dimensions */
    float       MinZ;         /* Min/max of clip Volume */
    float       MaxZ;
} EqViewPort9;
//our D3DMATRIX implementation
typedef struct _EQD3DMATRIX9
{
    union {
        struct {
            float        _11, _12, _13, _14;
            float        _21, _22, _23, _24;
            float        _31, _32, _33, _34;
            float        _41, _42, _43, _44;

        };
        float m[4][4];
    };
} EQD3DMATRIX9, *LPEQD3DMATRIX9;
//we are gonna need some pointers now for translating world coords to screen coords...
FakeCDisplay *pRender = 0;
void* pD3Ddevice = 0;
ScreenVector3 g_vWorldLocation,v3ScreenCoord;
EqViewPort9 g_viewPort;
EQD3DMATRIX9 g_projection, g_view, g_world;

//we also need a couple virtual functions defined and we can just put them FakeCDisplay 
FUNCTION_AT_VIRTUAL_ADDRESS(void FakeCDisplay::SetCursorPosition(void* This, int X,int Y,DWORD Flags), 0x2c);
FUNCTION_AT_VIRTUAL_ADDRESS(BOOL FakeCDisplay::ShowCursor(void* This,BOOL bShow),0x30);
FUNCTION_AT_VIRTUAL_ADDRESS(HRESULT FakeCDisplay::GetViewport(void*,void* pViewport),0xc0);
FUNCTION_AT_VIRTUAL_ADDRESS(HRESULT FakeCDisplay::GetTransform(void*,DWORD State,void* pMatrix),0xB4);

//ok magictime!
EQD3DMATRIX9* WINAPI EQD3DXMatrixMultiply(EQD3DMATRIX9 *pout, CONST EQD3DMATRIX9 *pm1, CONST EQD3DMATRIX9 *pm2)
{
	int i,j;
	for (i=0; i<4; i++)	{
		for (j=0; j<4; j++)	{
			pout->m[i][j] = pm1->m[i][0] * pm2->m[0][j] + pm1->m[i][1] * pm2->m[1][j] + pm1->m[i][2] * pm2->m[2][j] + pm1->m[i][3] * pm2->m[3][j];
		}
	}
	return pout;
}

ScreenVector3* WINAPI EQD3DXVec3TransformCoord(ScreenVector3 *pout, CONST ScreenVector3 *pv, CONST EQD3DMATRIX9 *pm)
{
	float norm;
	norm = pm->m[0][3] * pv->x + pm->m[1][3] * pv->y + pm->m[2][3] *pv->z + pm->m[3][3];
	if ( norm )	{
		pout->x = (pm->m[0][0] * pv->x + pm->m[1][0] * pv->y + pm->m[2][0] * pv->z + pm->m[3][0]) / norm;
		pout->y = (pm->m[0][1] * pv->x + pm->m[1][1] * pv->y + pm->m[2][1] * pv->z + pm->m[3][1]) / norm;
		pout->z = (pm->m[0][2] * pv->x + pm->m[1][2] * pv->y + pm->m[2][2] * pv->z + pm->m[3][2]) / norm;
	} else {
		pout->x = 0.0f;
		pout->y = 0.0f;
		pout->z = 0.0f;
	}
	return pout;
}

ScreenVector3* WINAPI EQD3DXVec3Project(ScreenVector3 *pout, CONST ScreenVector3 *pv, CONST EqViewPort9 *pviewport,
	CONST EQD3DMATRIX9 *pprojection, CONST EQD3DMATRIX9 *pview, CONST EQD3DMATRIX9 *pworld)
{
	EQD3DMATRIX9 m1, m2;
	ScreenVector3 vec;

	EQD3DXMatrixMultiply(&m1, pworld, pview);
	EQD3DXMatrixMultiply(&m2, &m1, pprojection);
	EQD3DXVec3TransformCoord(&vec, pv, &m2);
	pout->x = pviewport->X +  ( 1.0f + vec.x ) * pviewport->Width / 2.0f;
	pout->y = pviewport->Y +  ( 1.0f - vec.y ) * pviewport->Height / 2.0f;
	pout->z = pviewport->MinZ + vec.z * ( pviewport->MaxZ - pviewport->MinZ );
	return pout;
}

//ok now the function that use all the stuff above
bool MouseToPlayer(PlayerClient* pPlayer, DWORD position, BOOL bClick)
{
	if(pPlayer) {
		if(pRender = (FakeCDisplay*)g_pDrawHandler)
		{
			g_vWorldLocation.x = ((SPAWNINFO*)pPlayer)->Y;
			g_vWorldLocation.y = ((SPAWNINFO*)pPlayer)->X;
			g_vWorldLocation.z = ((SPAWNINFO*)pPlayer)->Z;//smack in the middle...
			if(position==1) {//head
				g_vWorldLocation.z = ((SPAWNINFO*)pPlayer)->FloorHeight+((SPAWNINFO*)pPlayer)->AvatarHeight;
			}
			if(position==2) {//feet
				g_vWorldLocation.z = ((SPAWNINFO*)pPlayer)->FloorHeight;
			}

			ScreenVector3 v3ScreenCoord = {0};
			pD3Ddevice = pRender->pDevice;
			((FakeCDisplay*)pD3Ddevice)->GetTransform(pD3Ddevice,EQD3DTS_VIEW, &g_view);
			((FakeCDisplay*)pD3Ddevice)->GetTransform(pD3Ddevice,EQD3DTS_PROJECTION, &g_projection);
			((FakeCDisplay*)pD3Ddevice)->GetTransform(pD3Ddevice,EQD3DTS_WORLD, &g_world);
			((FakeCDisplay*)pD3Ddevice)->GetViewport(pD3Ddevice,&g_viewPort);
			EQD3DXVec3Project(&v3ScreenCoord, &g_vWorldLocation, &g_viewPort, &g_projection, &g_view, &g_world);
			if(v3ScreenCoord.z >= 1) {
				WriteChatf("%s is not within view %.2f",((SPAWNINFO*)pPlayer)->DisplayedName,v3ScreenCoord.z);
				return false;
			}
			WriteChatf("%s is at %.2f, %.2f, %.2f before adjustment",((SPAWNINFO*)pPlayer)->DisplayedName,v3ScreenCoord.x,v3ScreenCoord.y,v3ScreenCoord.z);
			POINT pt = {0};
			pt.x = (int)v3ScreenCoord.x;
			pt.y = (int)v3ScreenCoord.y;
			//((FakeCDisplay*)pD3Ddevice)->ShowCursor(pD3Ddevice,1);
			//((FakeCDisplay*)pD3Ddevice)->SetCursorPosition(pD3Ddevice,pt.x,pt.y,1);//D3DCURSOR_IMMEDIATE_UPDATE

			MoveMouse(pt.x,pt.y,bClick);
			WriteChatf("%s is at %d, %d, %.2f after adjustment and mouse is at %d, %d",((SPAWNINFO*)pPlayer)->DisplayedName,pt.x,pt.y,v3ScreenCoord.z,EQADDR_MOUSE->X,EQADDR_MOUSE->Y);
		}
	}
	return false;
}
