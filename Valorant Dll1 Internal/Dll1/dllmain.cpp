// dllmain.cpp : Defines the entry point for the DLL application.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>
#include <d3d9.h> 
#include <algorithm>
#include <exception>
#include <string>
#include <filesystem>
#include <shlwapi.h>

#include <intrin.h>
using namespace			std;
#pragma comment(lib, "dwmapi")

#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_dx9.h"
#include "Imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Shlwapi.lib")

#include "hMain.h"
#include "Functions.h"
#include "uworld_decryption.h"
#include "offsets.h"
#include "_vars.h"
#include "_maths.h"
#include "mouse.h"
#include "image.h"
#include "simpleini.h"

HWND MyWnd = NULL;
HWND GameWnd = NULL;
RECT GameRect = { NULL };

string frameRate;

MSG Message;
const MARGINS Margin = { -1 ,-1, -1, -1 };
Vector2 LastRCS;

bool vischeck(uintptr_t entity) {

		return true;

		__try
		{
			auto tmp = Vector3(0, 0, 0);

			const auto line_sight = *reinterpret_cast<bool(__fastcall**)(uintptr_t, uintptr_t, Vector3*, bool)>(*reinterpret_cast<uintptr_t*>(g_local_player_controller) + 0x6C0);
			return line_sight(static_cast<uintptr_t>(uintptr_t(g_local_player_controller)), uintptr_t(entity), &tmp, false);
		}
		__except (1)
		{
			return false;
		}

}

void Normalize(Vector3& in)
{
	if (in.x > 89.f) in.x -= 360.f;
	else if (in.x < -89.f) in.x += 360.f;

	// in.y = fmodf(in.y, 360.0f);
	while (in.y > 180)in.y -= 360;
	while (in.y < -180)in.y += 360;
	in.z = 0;
}

Vector3 SmoothAim(Vector3 Camera_rotation, Vector3 Target, float SmoothFactor)
{
	Vector3 diff = Target - Camera_rotation;
	Normalize(diff);
	return Camera_rotation + diff / SmoothFactor;
}

__forceinline void Clamp(Vector3& Ang) {
	if (Ang.x < 0.f)
		Ang.x += 360.f;

	if (Ang.x > 360.f)
		Ang.x -= 360.f;

	if (Ang.y < 0.f) Ang.y += 360.f;
	if (Ang.y > 360.f) Ang.y -= 360.f;
	Ang.z = 0.f;
}

__forceinline Vector3 CalcAngle(Vector3 Src, Vector3& Dst) {
	Vector3 Delta = Src - Dst;
	const auto sqrtss = [](float in) {
		__m128 reg = _mm_load_ss(&in);
		return _mm_mul_ss(reg, _mm_rsqrt_ss(reg)).m128_f32[0];
	};

	float hyp = sqrtss(Delta.x * Delta.x + Delta.y * Delta.y + Delta.z * Delta.z);

	Vector3 Rotation{};
	Rotation.x = RadianToDegree(acosf(Delta.z / hyp));
	Rotation.y = RadianToDegree(atanf(Delta.y / Delta.x));
	Rotation.z = 0;
	if (Delta.x >= 0.0f) Rotation.y += 180.0f;
	Rotation.x += 270.f;
	return Rotation;
}

void Shoot() {
	if (mouseLoaded == false) {
		INPUT ip;
		ip.type = INPUT_KEYBOARD;
		ip.ki.wScan = 0;
		ip.ki.time = 0;
		ip.ki.dwExtraInfo = 0;

		// Press the "L" key
		ip.ki.wVk = 0x4C;
		ip.ki.dwFlags = 0;
		SendInput(1, &ip, sizeof(INPUT));
		ip.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &ip, sizeof(INPUT));
}
	if (mouseLoaded == true) {
		InjectedInputMouseInfo info = { 0 };
		info.MouseOptions = InjectedInputMouseOptions::LeftDown;
		InjectMouseInput(&info, 1);
		Sleep(1);
		info.MouseOptions = InjectedInputMouseOptions::LeftUp;
		InjectMouseInput(&info, 1);
	}
}

void moveMouse(int x, int y) {

	InjectedInputMouseInfo info = { 0 };
	info.DeltaX = x;
	info.DeltaY = y;
	info.MouseOptions = InjectedInputMouseOptions::MoveNoCoalesce;
	InjectMouseInput(&info, 1);

}

void Flick(Vector3 target, Vector3 camera_position, Vector3 camera_rotation, float camera_fov, float smooth)		 {

	const auto relative_pos = target - camera_position;

	auto pitch = -(acos(relative_pos.z / camera_position.Distance(target)) * 180.0f / M_PI - 90.0f);
	auto yaw = atan2(relative_pos.y, relative_pos.x) * 180.0f / M_PI;

	//Vector3 rotation;
	//rotation.z = 0;
	//rotation.x = RadianToDegree(acosf(relative_pos.z / camera_position.Distance(target)));
	//rotation.y = RadianToDegree(atanf(relative_pos.y / relative_pos.x));
	//if (rotation.x >= 0.0f) rotation.y += 180.0f;
	//rotation.x += 270.f;
	//Clamp(rotation);

	Vector3 aimangles = Vector3((pitch - camera_rotation.x) / smooth + camera_rotation.x, (yaw - camera_rotation.y) / smooth + camera_rotation.y, 0.f); //: Vector3(pitch, yaw, 0.f)


	write(g_local_player_controller + offsets::control_rotation, &aimangles);

}
	
void AimRCS(Vector3 target, Vector3 camera_position, Vector3 camera_rotation, float camera_fov, float smooth) {

	Vector3 ConvertRotation = camera_rotation;
	Normalize(ConvertRotation);

	Vector3 ControlRotation = read<Vector3>(g_local_player_controller + offsets::control_rotation);
	Vector3 DeltaRotation = ConvertRotation - ControlRotation;
	Normalize(DeltaRotation);	

	if (abs(DeltaRotation.x) > 10) { return; } //anti hs aimpunsh
	
	//char delta[50];
	//sprintf(delta, "Delta.x: %.f, y: %.f", DeltaRotation.x, DeltaRotation.y);
	//draw.Text(50, 100, delta, D3DCOLOR_ARGB(255, 255, 255, 255), true, kCenter);


	ConvertRotation = target - (DeltaRotation * smooth);
	Normalize(ConvertRotation);

	//char convert[50];
	//sprintf(convert, "Convert.x: %.f, y: %.f", ConvertRotation.x, ConvertRotation.y);
	//draw.Text(50, 140, convert, D3DCOLOR_ARGB(255, 255, 255, 255), true, kCenter);

	Vector3 Smoothed = SmoothAim(camera_rotation, ConvertRotation, smooth);
	Smoothed -= (DeltaRotation / smooth);
	Clamp(Smoothed);

	//char Smooths[50];
	//sprintf(Smooths, "Convert.x: %.f, y: %.f", Smoothed.x, Smoothed.y);
	//draw.Text(50, 180, Smooths, D3DCOLOR_ARGB(255, 255, 255, 255), true, kCenter);

	//Normalize(Smoothed);

	if (Smoothed.x == 0 && Smoothed.y == 0) return;

	//const auto aimangles = Vector3((ControlRotation.x - camera_rotation.x) / flickSmooth + camera_rotation.x, (ControlRotation.y	 - camera_rotation.y) / flickSmooth + camera_rotation.y, 0.f); //: Vector3(pitch, yaw, 0.f)
	write(g_local_player_controller + offsets::control_rotation, &Smoothed);
}

void RCS(Vector3 camera_position, Vector3 camera_rotation, float camera_fov, float smooth) {

	Clamp(camera_rotation);
	Vector3 ControlRotation = read<Vector3>(g_local_player_controller + offsets::control_rotation);
	Clamp(ControlRotation);
	float screen_width = Width;
	float screen_height = Height;

	Vector3 punchAngle = (camera_rotation - ControlRotation);

	Clamp(punchAngle);
	Normalize(punchAngle);

	int centerX = screen_width / 2; // may need to add or subtract to make perfect
	int centerY = screen_height / 2;

	int dx = screen_width / 90;
	int dy = screen_height / 90;

	centerX += (dx * (punchAngle.y));
	centerY -= (dy * (punchAngle.x));

	auto trueX = (-(centerX - ScreenCenterX))- LastRCS.x;
	auto trueY = (-(centerY - ScreenCenterY))- LastRCS.y;

	LastRCS.x = -(centerX - ScreenCenterX);
	LastRCS.y = -(centerY - ScreenCenterY);


	moveMouse(trueX * smooth, trueY * smooth);
}

bool canTriggerENEMY(Enemy i) {

	Vector3 camera_position = read<Vector3>(g_camera_manager + offsets::camera_position);
	Vector3 camera_rotation = read<Vector3>(g_camera_manager + offsets::camera_rotation);
	float camera_fov = read<float>(g_camera_manager + offsets::camera_fov);

	int bonecount = read<int>(i.mesh_ptr + offsets::bone_count);
	for (int b = 1; b < bonecount; b++) {

		Vector3 bonePos = getBonePosition(i, b);
		Vector2 bonePos_w2s = worldToScreen(bonePos, camera_position, camera_rotation, camera_fov);
		Vector2 bonePos_w2s_relative = Vector2(bonePos_w2s.x - ScreenCenterX, bonePos_w2s.y - ScreenCenterY);
		float bonePos_from_crosshair = distanceFromCrosshair(bonePos_w2s_relative);
		if (abs(bonePos_from_crosshair) < 2) { return true; }
	}
	return false;

}

void Kill(Enemy target, Vector3 camera_position, Vector3 camera_rotation, float camera_fov) {

	float health = read<float>(target.damage_handler_ptr + offsets::health);
	Vector3 targetBone;

	//AIM AT BONES IF HP < 
	if (health < 50) {
	targetBone = getBonePosition(target, 6);
	}
	else targetBone = getBonePosition(target, 8);
	//
	if (targetBone.x == 0 && targetBone.y == 0 && targetBone.z == 0)return;
		
	//Vector3 sniperBone;
	//sniperBone = getBonePosition(target, 6);
	//
	Vector2 targetBone_w2s = worldToScreen(targetBone, camera_position, camera_rotation, camera_fov);
	Vector2 targetBone_w2s_relative = Vector2(targetBone_w2s.x - ScreenCenterX, targetBone_w2s.y - ScreenCenterY);
	float targetBone_from_crosshair = distanceFromCrosshair(targetBone_w2s_relative);
	//
	//Vector2 sniperBone_w2s = worldToScreen(sniperBone, camera_position, camera_rotation, camera_fov);
	//Vector2 sniperBone_w2s_relative = Vector2(sniperBone_w2s.x - ScreenCenterX, sniperBone_w2s.y - ScreenCenterY);
	//float sniperBone_from_crosshair = distanceFromCrosshair(sniperBone_w2s_relative);

	Vector3 AimAngles = CalcAngle(camera_position, targetBone);
	Clamp(AimAngles);

	if (mouseLoaded == false) {



		//FUNZIONA TUTTO QUI V
		if (isKeyTapped(FlickKey)) {
			if (lastKeySmashed) {
				Flick(targetBone, camera_position, camera_rotation, camera_fov, flickSmooth);
				if (targetBone_from_crosshair < 3) {
					Shoot();
					lastKeySmashed = false;
				}
			}
			if (!lastKeySmashed) {
				AimRCS(AimAngles, camera_position, camera_rotation, camera_fov, RCSmooth + 5.f);
				Shoot();
			}
		}	

		if (isKeyPressed(VK_LBUTTON)) {

			AimRCS(AimAngles, camera_position, camera_rotation, camera_fov, RCSmooth);
		}


}

	if (mouseLoaded == true) {


		////FUNZIONA TUTTO QUI V
		if (isKeyTapped(FlickKey)) {
			if (lastKeySmashed) {
				moveMouse(targetBone_w2s_relative.x * mouseFlick, targetBone_w2s_relative.y * mouseFlick);
				if (targetBone_from_crosshair < 3) {
					Shoot();
					lastKeySmashed = false;
				}
			}
		}	

		if (isKeyPressed(VK_LBUTTON)) {

			AimRCS(AimAngles, camera_position, camera_rotation, camera_fov, RCSmooth);
		}

		if (isKeyPressed(TriggerkKey)) {
			if (abs(targetBone_w2s_relative.x) < 5) {
				Shoot();
			}

		}

	}

	//TRIGGERBOTS
	if (isKeyPressed(TriggerkKey)) {
		if (canTriggerENEMY(target)) {
			Shoot();
		}
	}

}

void drawRCS(Vector3 camera_rotation) {
	Clamp(camera_rotation);
	Vector3 ControlRotation = read<Vector3>(g_local_player_controller + offsets::control_rotation);
	Clamp(ControlRotation);
	float screen_width = Width;
	float screen_height = Height;	

	Vector3 punchAngle = (camera_rotation - ControlRotation);
	//if (camera_rotation.y < ControlRotation.y) {
	//	punchAngle.y = -punchAngle.y;
	//}
	Clamp(punchAngle);
	Normalize(punchAngle);

	int centerX = screen_width / 2; // may need to add or subtract to make perfect
	int centerY = screen_height / 2;

	int dx = screen_width / 90;
	int dy = screen_height / 90;

	centerX += (dx * (punchAngle.y));
	centerY -= (dy * (punchAngle.x));

	//int centerX = ScreenCenterX; // may need to add or subtract to make perfect
	//int centerY = ScreenCenterY;

	//int dx = screen_width / 90;
	//int dy = screen_height / 90;

	//centerX -= (dx * (punchAngle.y));
	//centerY += (dy * (punchAngle.x));

	//punchAngle.y *= 0.6;
	//int a = Width / 2 - (Width / 90 * punchAngle.y);
	//int b = Height / 2 + (Height / 90 * punchAngle.x)

	//const auto aimangles = Vector3(camera_rotation.x + (punchAngle.x / RCSmooth),camera_rotation.y + (punchAngle.y / RCSmooth), 0.f); //: Vector3(pitch, yaw, 0.f)
	//write(g_local_player_controller + offsets::control_rotation, &aimangles, sizeof(Vector3));

	DrawCircle(centerX, centerY, 3, 10, D3DCOLOR_ARGB(255, 0, 255, 0));
}

void ESP(Enemy i) {

	Vector3 camera_position = read<Vector3>(g_camera_manager + offsets::camera_position);
	Vector3 camera_rotation = read<Vector3>(g_camera_manager + offsets::camera_rotation);
	float camera_fov = read<float>(g_camera_manager + offsets::camera_fov);

	//////////////////////////////////////////////////////////////////////////////////////

	Vector3 head_position = getBonePosition(i, 8);
	Vector2 head_w2s = worldToScreen(head_position, camera_position, camera_rotation, camera_fov);
	Vector3 foot_position = getBonePosition(i, 0);
	Vector2 foot_w2s = worldToScreen(foot_position, camera_position, camera_rotation, camera_fov);

	Vector2 Highest = Vector2(head_w2s.x, head_w2s.y);
	Highest.y -= 5.f;
	Vector2 Lowest = Vector2(head_w2s.x, foot_w2s.y);
	float Diff = abs(Highest.y - Lowest.y);

	//D3DCOLOR_ARGB(255, 255, 255, 255)

	//DRAW BOX
	DrawBOX(Highest.x, Highest.y, Highest.x, Lowest.y, Diff / 2.5, i.is_visible ? D3DCOLOR_ARGB(255, 255, 0, 0) : D3DCOLOR_ARGB(255, 0, 0, 255));

#pragma region DRAW NAME

	//DRAW NAME
	char name_char[64];
	auto name = i.actor_name;
	sprintf(name_char, name.c_str());

	uintptr_t RootComp = read<uintptr_t>(i.actor_ptr + offsets::root_component);
	Vector3 RootPos = read<Vector3>(RootComp + offsets::root_position);

	uintptr_t localRootComp = read<uintptr_t>(g_local_player_pawn + offsets::root_component);
	auto localPos = read<Vector3>(localRootComp + offsets::root_position);
	float fDistance = localPos.Distance(RootPos) / 100.f;

	char dist_char[64];
	sprintf(dist_char, ("%.f"), fDistance);

	DrawFilled(head_w2s.x - (strlen(dist_char) * 7 / 2) - 27, head_w2s.y - 25, (3 * 7 / 2) + 16, 18, D3DCOLOR_ARGB(255, 255, 0, 0));
	DrawFilled(head_w2s.x - (strlen(dist_char) * 7 / 2) - 1, head_w2s.y - 25, strlen(name_char) * 7 + 20, 18, D3DCOLOR_ARGB(255, 0, 0, 0));
	//font->DrawTextX(head_w2s.x - (strlen(dist_char) * 7 / 2) - (fDistance < 10.f ? 17 : 21), head_w2s.y - 25, Silver_Menu, dist_);
	//font->DrawTextX(head_w2s.x - (strlen(dist_char) * 7 / 2) + 7, head_w2s.y - 25, vis_col, dist_char);
	//draw.Text(head_w2s.x, head_w2s.y - 25, name_char, i.is_visible ? D3DCOLOR_ARGB(255, 255, 0, 0) : D3DCOLOR_ARGB(255, 0, 0, 255), 0, kCenter);
	//draw.Text(head_w2s.x, foot_w2s.y + 25, dist_char, D3DCOLOR_ARGB(255, 255, 255, 255), 0, kCenter);
	draw.Text(head_w2s.x - (strlen(dist_char) * 7 / 2) - (fDistance < 10.f ? 17 : 21), head_w2s.y - 25, dist_char, D3DCOLOR_ARGB(255, 255, 255, 255));
	draw.Text(head_w2s.x - (strlen(dist_char) * 7 / 2) + 7, head_w2s.y - 25, name_char, i.is_visible ? D3DCOLOR_ARGB(255, 255, 0, 0) : D3DCOLOR_ARGB(255, 0, 0, 255));

#pragma endregion

	//DRAW HEALTHBAR
	float health = read<float>(i.damage_handler_ptr + offsets::health);
	auto health_color = GetHealthColor(health);
	auto health_value = (Diff * health) / 100;
	DrawFilled(foot_w2s.x - ((Diff / 2.5) / 2) - (Diff/8), foot_w2s.y, 4, -health_value, health_color);

}



void Draws() {
	float closestDistance = FLT_MAX;

	Vector3 camera_position = read<Vector3>(g_camera_manager + offsets::camera_position);
	Vector3 camera_rotation = read<Vector3>(g_camera_manager + offsets::camera_rotation);
	float camera_fov = read<float>(g_camera_manager + offsets::camera_fov);
	
	float myHealth = read<float>(g_local_damage_handler + offsets::health);

	if (myHealth != 0 && g_local_player_pawn != NULL) {
		if (wasDead == true) { canLoop = true; wasDead = false; }
		//FOV
		//DrawCircle(ScreenCenterX, ScreenCenterY, fovValue, 50, D3DCOLOR_ARGB(255, 255, 255, 255));		

		std::vector<Enemy> local_enemy_collection = enemy_collection;
		Enemy ClosestEnemy;
		bool enemyIsValid = false;

		if (!enemy_collection.empty()) {
			draw.FPSCheck(frameRate);
			draw.Text(40, 40, frameRate, AIM_toggle ? D3DCOLOR_ARGB(255, 255, 255, 255) : D3DCOLOR_ARGB(255, 255, 0, 0), true, kLeft); //AIM_toggle ? D3DCOLOR_ARGB(255, 255, 255, 255) : D3DCOLOR_ARGB(255, 255, 0, 0)

			for (auto i : local_enemy_collection) {
				
				//GET ENEMY AND HEALTH
				Enemy enemy = i; 
				uintptr_t enemy_team_component = read<uintptr_t>(enemy.player_state_ptr + offsets::team_component);

				if (i.actor_ptr == NULL || i.actor_ptr == g_local_player_pawn) {
					continue;
				}

				float health = read<float>(enemy.damage_handler_ptr + offsets::health);
				int team = read<int>(enemy_team_component + offsets::team_id);

				if (health <= 0 || health > 100) {
					continue;
				}


				
				float last_render_time = read<float>(enemy.mesh_ptr + offsets::last_render_time);
				float last_submit_time = read<float>(enemy.mesh_ptr + offsets::last_submit_time);
				bool is_visible = last_render_time + 0.06F >= last_submit_time;//man only read func
				bool dormant = read<bool>(enemy.actor_ptr + offsets::dormant);
				if (!dormant || !is_visible) { //!dormant || !is_visible
					continue;
				}

				enemy.is_visible = vischeck(enemy.actor_ptr); //vischeck(enemy.actor_ptr)

				Vector3 head_position = getBonePosition(enemy, 8); // 8 = head bone
				Vector3 root_position = read<Vector3>(enemy.root_component_ptr + offsets::root_position);
				Vector3 foot_position = getBonePosition(enemy, 0);
				Vector2 root_w2s = worldToScreen(root_position, camera_position, camera_rotation, camera_fov);
				Vector2 foot_w2s = worldToScreen(foot_position, camera_position, camera_rotation, camera_fov);
				if (head_position.z <= root_position.z) {
					continue;
				}

				float distance_modifier = camera_position.Distance(head_position) * 0.001F;

				Vector2 head_w2s = worldToScreen(head_position, camera_position, camera_rotation, camera_fov);
				Vector2 head_w2s_relative = Vector2(head_w2s.x - ScreenCenterX, head_w2s.y - ScreenCenterY);
				float head_from_crosshair = distanceFromCrosshair(head_w2s_relative);
				//i.distanceFromCrosshair = head_from_crosshair;



				//DEFINE TARGET FOR AIMBOT
				if (head_from_crosshair < fovValue && head_from_crosshair < closestDistance && enemy.is_visible) { // && enemy.is_visible

					closestDistance = head_from_crosshair;
					ClosestEnemy = i;
					enemyIsValid	= true;
				}


				if (VISUALS_toggle == TRUE) {
					//disegni

					ESP(enemy);

				}

			}
		}

		if (AIM_toggle == true) {
		
			Kill(ClosestEnemy, camera_position, camera_rotation, camera_fov);
		}



		if (!isKeyPressed(FlickKey)) {
			lastKeySmashed = true;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////
		
		//
		//Vector3 ConvertRotation = camera_rotation;
		//Normalize(ConvertRotation);
		//
		//Vector3 ControlRotation = read<Vector3>(g_local_player_controller + offsets::control_rotation);
		//Vector3 DeltaRotation = ConvertRotation - ControlRotation;
		//Normalize(DeltaRotation);
		//
		//char delta[50];
		//sprintf(delta, "Delta.x: %.f, y: %.f", DeltaRotation.x, DeltaRotation.y);
		//draw.Text(50, 100, delta, D3DCOLOR_ARGB(255, 255, 255, 255), true, kCenter);

	}
	else { wasDead = true; }

	//char delta[50];
	//sprintf(delta, "wasDead: %d, canLoop: %d", wasDead, canLoop);
	//draw.Text(100, 100, delta, D3DCOLOR_ARGB(255, 255, 255, 255), true, kCenter);
}

void Render()
{
	draw.GetDevice()->Clear(NULL, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, NULL);
	draw.GetDevice()->BeginScene();

	GameWnd = FindWindow(("UnrealWindow"), 0);

	if (!GameWnd)
		ExitProcess(0);

	if (GameWnd == GetForegroundWindow() || GetActiveWindow() == GetForegroundWindow())
	{

		draw.GetDevice()->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);



		Draws();
		
		if (VISUALS_toggle == true) {
			draw.Image(ScreenCenterX-65, -1, &draw.myTexture);
		}

	}
	
	draw.GetDevice()->EndScene();
	draw.GetDevice()->Present(NULL, NULL, NULL, NULL);
}


unsigned __stdcall MainLoop(LPVOID lp)
{
	HideThread(GetCurrentThread());
	ZeroMemory(&Message, sizeof(MSG));

	while (Message.message != WM_QUIT)
	{
		if (!draw.IsInitialized())
		{
			D3DXCreateFontA(draw.GetDevice(), 16, 7, FW_NORMAL, 1, false, FS_CHINESESIMP, OUT_DEFAULT_PRECIS,
				ANTIALIASED_QUALITY,    // default Quality
				DEFAULT_PITCH | FF_DONTCARE, ("Arial"), &draw.font);
			D3DXCreateLine(draw.GetDevice(), &dx_Line); //xwwwwwwqq

			D3DXCreateTextureFromFileInMemory(draw.GetDevice(), &dogma_png, sizeof(dogma_png), &draw.myTexture.imagetex);
			D3DXCreateSprite(draw.GetDevice(), &draw.myTexture.sprite);

			//  input.Init(MyWnd);
			draw.Init();
		}


		if (GetAsyncKeyState(AimToggleKey) & 1) AIM_toggle = !AIM_toggle;
		if (GetAsyncKeyState(VisualsToggleKey) & 1) VISUALS_toggle = !VISUALS_toggle;

		Render();
		
	}

	DestroyWindow(MyWnd);
	CleanupD3D();
	ExitProcess(0);
	return Message.wParam;
}






unsigned __stdcall retreiveData(LPVOID lp)	 {
	HideThread(GetCurrentThread());

	while (true) {

		uintptr_t world = decrypt_uworld();
		global_world = world;
		uintptr_t game_instance = read<uintptr_t>(world + offsets::game_instance);
		uintptr_t persistent_level = read<uintptr_t>(world + offsets::persistent_level);
		uintptr_t local_player_array = read<uintptr_t>( game_instance + offsets::local_player_array);
		uintptr_t local_player = read<uintptr_t>(local_player_array);
		uintptr_t local_player_controller = read<uintptr_t>(local_player + offsets::local_player_controller);
		uintptr_t local_player_pawn = read<uintptr_t>(local_player_controller + offsets::local_player_pawn);
		uintptr_t local_damage_handler = read<uintptr_t>(local_player_pawn + offsets::damage_handler);
		uintptr_t local_player_state = read<uintptr_t>(local_player_pawn + offsets::player_state);
		uintptr_t local_team_component = read<uintptr_t>(local_player_state + offsets::team_component);
		int local_team_id = read<int>(local_team_component + offsets::team_id);


		uintptr_t camera_manager = read<uintptr_t>(local_player_controller + offsets::camera_manager);

		uintptr_t actor_array = read<uintptr_t>(persistent_level + offsets::actor_array);

		int actor_count = read<int>(persistent_level + offsets::actor_count);

		g_local_player_controller = local_player_controller;
		g_local_player_pawn = local_player_pawn;
		g_local_damage_handler = local_damage_handler;
		g_camera_manager = camera_manager;
		g_local_team_id = local_team_id;

	/*	GetEnemies();*/

		if (canLoop == true) {
			std::vector<Enemy> temp_enemy_collection{};
			size_t size = sizeof(uintptr_t);
			for (int i = 0; i < actor_count; i++) { //i < actor_count 2000

				//printf("IM LOOPING! ACTOR N: %i\n", i);

				uintptr_t actor = read<uintptr_t>(actor_array + (i * size));
				if (actor == 0x00) {
					continue;
				}
				uintptr_t unique_id = read<uintptr_t>(actor + offsets::unique_id);
				if (unique_id != 0x11E0101) {
					continue;
				}


				int unique_ID = read<int>(actor + offsets::ObjID);
				if (GetCharacterName(unique_ID) == "???") { continue; }

				uintptr_t mesh = read<uintptr_t>(actor + offsets::mesh_component);
				if (!mesh) {
					continue;
				}

				uintptr_t player_state = read<uintptr_t>(actor + offsets::player_state);
				uintptr_t team_component = read<uintptr_t>(player_state + offsets::team_component);
				int team_id = read<int>(team_component + offsets::team_id);
				int bone_count = read<int>(mesh + offsets::bone_count);
				bool is_bot = bone_count == 103;
				if (team_id == g_local_team_id) { //&& !is_bot
					continue;
				}


				uintptr_t damage_handler = read<uintptr_t>(actor + offsets::damage_handler);
				uintptr_t root_component = read<uintptr_t>(actor + offsets::root_component);
				uintptr_t bone_array = read<uintptr_t>(mesh + offsets::bone_array);

				Enemy enemy{
					actor,
					damage_handler,
					player_state,
					root_component,
					mesh,
					bone_array,
					false,
					GetCharacterName(unique_ID)
				};


				temp_enemy_collection.push_back(enemy);
			}

			enemy_collection.clear();
			enemy_collection = temp_enemy_collection;
			canLoop = false;
		
		}
	
		Sleep(1000);
	}
}



unsigned _stdcall setupMouse(LPVOID lp) {

	HideThread(GetCurrentThread());

	const HMODULE user32 = LoadLibraryA("user32.dll");
	if (!user32)
	{
		printf("user32.dll failed to load\n");
		mouseLoaded = false;
		return 0;
	}

	InjectMouseInput = reinterpret_cast<InjectMouseInput_t>(GetProcAddress(user32, "InjectMouseInput"));
	if (!InjectMouseInput)
	{
		printf("MouseInput not found\n");
		mouseLoaded = false;
		return 0;
	}

	char value[255];
	DWORD BufferSize = 8192;
	RegGetValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ReleaseID", RRF_RT_ANY, NULL, (PVOID)&value, &BufferSize);

	if (strcmpi(value, "1709") == 0)
	{
		mouseLoaded = true;
		return 1;
	}

	if (strcmpi(value, "1803") == 0)
	{
		mouseLoaded = true;
		return 1;
	}

	mouseLoaded = false;
	printf("WINVER not supported\n");
	return 0;
}

unsigned _stdcall setupConfig(LPVOID lp) {

	HideThread(GetCurrentThread());
	DWORD BufferSize = 8192;

	RegGetValue(HKEY_CURRENT_USER, NULL, "drivE - FLICK", RRF_RT_DWORD, NULL, (PVOID)&FlickKey, &BufferSize);
	if (FlickKey > 165 || FlickKey < 1) { FlickKey == VK_SHIFT; }

	RegGetValue(HKEY_CURRENT_USER, NULL, "drivE - TRIGGER", RRF_RT_DWORD, NULL, (PVOID)&TriggerkKey, &BufferSize);
	if (TriggerkKey > 165 || TriggerkKey < 1) { TriggerkKey == VK_MENU; }

	RegGetValue(HKEY_CURRENT_USER, NULL, "drivE - AIMTOGGLE", RRF_RT_DWORD, NULL, (PVOID)&AimToggleKey, &BufferSize);
	if (AimToggleKey > 165 || AimToggleKey < 1) { AimToggleKey == VK_F5; }

	RegGetValue(HKEY_CURRENT_USER, NULL, "drivE - VISUALTOGGLE", RRF_RT_DWORD, NULL, (PVOID)&VisualsToggleKey, &BufferSize);
	if (VisualsToggleKey > 165 || VisualsToggleKey < 1) { VisualsToggleKey == VK_F10; }

	return 0;

}

void setupConfig() {

	//WCHAR exeDir[MAX_PATH] = {};
	//GetModuleFileNameW(NULL, exeDir, MAX_PATH);
	//PathRemoveFileSpecW(exeDir);
	//WCHAR iniDir[MAX_PATH] = {};
	//PathCombineW(iniDir, exeDir, L"drive.config.ini");

	//
	//FlickKey = GetPrivateProfileInt("KEY", "FLICK", VK_SHIFT, WStringToString(iniDir).c_str());
	//TriggerkKey = GetPrivateProfileInt("KEY", "TRIGGER", VK_MENU, WStringToString(iniDir).c_str());
	//AimToggleKey = GetPrivateProfileInt("TOGGLE", "AIM", VK_F5, WStringToString(iniDir).c_str());
	//VisualsToggleKey = GetPrivateProfileInt("TOGGLE", "VISUALS", VK_F10, WStringToString(iniDir).c_str());

	//CSimpleIniA ini;
	//SI_Error rc = ini.LoadFile("drive.config.ini");
	//if (rc < 0) { return 0; };


	//FlickKey = (int)ini.GetDoubleValue("KEY", "FLICK");
	//if (FlickKey > 165 || FlickKey < 1) { FlickKey == VK_SHIFT; }

	//TriggerkKey = (int)ini.GetDoubleValue("KEY", "TRIGGER");
	//if (TriggerkKey > 165 || TriggerkKey < 1) { TriggerkKey == VK_MENU; }

	//AimToggleKey = (int)ini.GetDoubleValue("TOGGLE", "AIM");
	//if (AimToggleKey > 165 || AimToggleKey < 1) { AimToggleKey == VK_F5; }

	//VisualsToggleKey = (int)ini.GetDoubleValue("TOGGLE", "VISUALS");
	//if (VisualsToggleKey > 165 || VisualsToggleKey < 1) { VisualsToggleKey == VK_F10; }

	//return 1;


}

void Init() {

	while (!GetModuleHandleA("d3d9.dll")) {
		Sleep(1);
	}
	std::string yey = "Tranquillity";
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = 0;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(RGB(0, 0, 0));
	wc.lpszMenuName = NULL;
	wc.lpszClassName = yey.c_str();
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	if (!RegisterClassEx(&wc))
		exit(1);

	const char* wnd_str = ("UnrealWindow");
	GameWnd = FindWindow(wnd_str, 0);
	RtlSecureZeroMemory(&wnd_str, sizeof(wnd_str));
	ZeroMemory(&GameRect, sizeof(GameRect));
	GetWindowRect(GameWnd, &GameRect);
	Width = GameRect.right - GameRect.left;
	Height = GameRect.bottom - GameRect.top;
	DWORD dwStyle = GetWindowLong(GameWnd, GWL_STYLE);
	if (dwStyle & WS_BORDER)
	{
		GameRect.top += 32;
		Height -= 39;
	}
	ScreenCenterX = Width / 2;
	ScreenCenterY = Height / 2;

	MyWnd = CreateWindowEx(
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		yey.c_str(),
		yey.c_str(),//""
		WS_POPUP | WS_VISIBLE,
		GameRect.left, GameRect.top, Width, Height,
		NULL, NULL, 0, NULL);

	SetLayeredWindowAttributes(MyWnd, RGB(0, 0, 0), 255, LWA_ALPHA);
	DwmExtendFrameIntoClientArea(MyWnd, &Margin);

	TextureItems myTexture;



	if (D3D9Init(MyWnd))
	{

		//setupConfig();
		_beginthreadex(0, 0, setupMouse, 0, 0, 0);
		_beginthreadex(0, 0, setupConfig, 0, 0, 0);
		//
		// 
		//

		_beginthreadex(0, 0, retreiveData, 0, 0, 0);
		_beginthreadex(0, 0, MainLoop, 0, 0, 0);

		PlaySound(("C:\\Windows\\media\\Windows Error"), NULL, SND_ALIAS);
	}

}

void entryP() {

	//AllocConsole();
	//freopen("CONOUT$", "w", stdout);





    printf("[RYU] - TRANQUILLITY LOADED : - )\n");


	// MAIN LOOP!
	Init();	
	//_beginthreadex(0, 0, test, 0, 0, 0);


}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{

    HideThread(hModule);

    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        //MessageBox(NULL, "<3 _ <3", "TRANQUILLITY", MB_ICONINFORMATION);
		entryP();
        CloseHandle(hModule);
    }
    return TRUE;
}

