#include "ui.h"

#include <inttypes.h>
#include <imgui/imgui.h>
#include <stdio.h>

EXTERN_C {
	#include "strokes.h"
}

static bool windowFocused;
static double lastMouseX, lastMouseY;
static float scrollAccumulator;

static void(*glPrepFrameStateFunc)(int, int, int, int);
static void(*glUploadDataFunc)(uint32_t, const void*, uint32_t, const void*);
static void(*glDrawElementFunc)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, const void*);

void lb_ui_windowFocusCallback(bool focused) {
	windowFocused = focused;
}

void lb_ui_scrollCallback(double x, double y) {
	scrollAccumulator += y;
}

void lb_ui_cursorPosCallback(double x, double y) {
	lastMouseX = x;
	lastMouseY = y;
}

void lb_ui_mouseButtonCallback(int button, int action, int mods) {
	if(button < 0 || button > 3) {
		return;
	}
	
	ImGuiIO& io = ImGui::GetIO();
	io.MouseDown[button] = (action == 1);
	
	//TODO: Make resilient to sub-frame clicks
	// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
	// io.MouseDown[i] = g_MouseJustPressed[i] || glfwGetMouseButton(g_Window, i) != 0;
	// g_MouseJustPressed[i] = false;
}

void lb_ui_charCallback(unsigned int codePoint) {
	ImGuiIO& io = ImGui::GetIO();
	if (codePoint > 0 && codePoint < 0x10000) {
		io.AddInputCharacter((unsigned short)codePoint);
	}
}

void lb_ui_keyCallback(int key, int scancode, int action, int mods) {
	ImGuiIO& io = ImGui::GetIO();
	if (action == 1) io.KeysDown[key] = true;
	if (action == 0) io.KeysDown[key] = false;
}


static void renderImGuiDrawLists(ImDrawData* data) {
	ImGuiIO& io = ImGui::GetIO();
	
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
	int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
	if (fb_width == 0 || fb_height == 0)
		return;
	
	data->ScaleClipRects(io.DisplayFramebufferScale);
	
	glPrepFrameStateFunc(io.DisplaySize.x, io.DisplaySize.y, fb_width, fb_height);
	
	for(int n = 0; n < data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = data->CmdLists[n];
		const ImDrawIdx* idx_buffer_offset = 0;
		
		glUploadDataFunc(cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (void*)cmd_list->VtxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (void*)cmd_list->IdxBuffer.Data);
		
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback) {
				pcmd->UserCallback(cmd_list, pcmd);
			} else {
				glDrawElementFunc(
					(intptr_t)pcmd->TextureId,
					(int)pcmd->ClipRect.x, (int)(fb_height - pcmd->ClipRect.w), (int)(pcmd->ClipRect.z - pcmd->ClipRect.x), (int)(pcmd->ClipRect.w - pcmd->ClipRect.y),
					pcmd->ElemCount, sizeof(ImDrawIdx), idx_buffer_offset);
			}
			idx_buffer_offset += pcmd->ElemCount;
		}
	}
}

static struct {
	bool showDemoPanel = false;
} guiState;

EXTERN_C void lb_ui_init(
	void(*glInit)(const unsigned char*, const int, const int, unsigned int*),
	void(*glPrepFrameState)(int, int, int, int),
	void(*glUploadData)(uint32_t, const void*, uint32_t, const void*),
	void(*glDrawElement)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, const void*))
{
	
	glPrepFrameStateFunc = glPrepFrameState;
	glUploadDataFunc = glUploadData;
	glDrawElementFunc = glDrawElement;
	
	windowFocused = true;
	scrollAccumulator = 0.0f;
	lastMouseX = lastMouseY = 0;
	
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = NULL;
	
	// Build texture atlas
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.
	
	unsigned int fontTextureID;
	glInit(pixels, width, height, &fontTextureID);
	
	// Store our identifier
	io.Fonts->TexID = (void *)(intptr_t) fontTextureID;
	
	io.RenderDrawListsFn = renderImGuiDrawLists;
	
	ImGui::StyleColorsDark();
}

EXTERN_C void lb_ui_destroy(void(*glDestroy)()) {
	ImGui::GetIO().Fonts->TexID = 0;
	ImGui::Shutdown();
	glDestroy();
}

static void drawMainMenuBar() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::MenuItem("Show Demo Panel")) guiState.showDemoPanel = true;
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

static void drawTimeline() {
	const int timeline_height = 18;
	const int handle_height = 18;
	const int total_height = timeline_height + handle_height;

	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, total_height));
	ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - total_height));
	ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	const ImVec2 timeline_min = ImVec2(0, io.DisplaySize.y - timeline_height);
	const ImVec2 timeline_max = ImVec2(io.DisplaySize.x, io.DisplaySize.y);
	float playhead_pos_x = lb_strokes_timelinePosition / lb_strokes_timelineDuration * io.DisplaySize.x;
	draw_list->AddRectFilled(timeline_min, timeline_max, ImGui::GetColorU32(ImGuiCol_Border), 0);
	draw_list->AddRectFilled(timeline_min, ImVec2(timeline_min.x + playhead_pos_x, timeline_max.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 0);
	
	bool mouse_hovering_playhead = ImGui::IsMouseHoveringRect(ImVec2(timeline_min.x + playhead_pos_x - timeline_height/2, timeline_min.y), ImVec2(timeline_min.x + playhead_pos_x + timeline_height/2, timeline_max.y));
	bool mouse_hovering_timeline = ImGui::IsMouseHoveringRect(timeline_min, timeline_max);

	ImGuiCol playhead_color = (lb_strokes_draggingPlayhead || mouse_hovering_playhead) ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive;
	draw_list->AddTriangleFilled(
		ImVec2(timeline_min.x + playhead_pos_x, timeline_min.y),
		ImVec2(timeline_min.x + playhead_pos_x - timeline_height/2, timeline_min.y + timeline_height/2),
		ImVec2(timeline_min.x + playhead_pos_x + timeline_height/2, timeline_min.y + timeline_height/2),
		ImGui::GetColorU32(playhead_color));
	draw_list->AddTriangleFilled(
		ImVec2(timeline_min.x + playhead_pos_x - timeline_height/2, timeline_min.y + timeline_height/2),
		ImVec2(timeline_min.x + playhead_pos_x + timeline_height/2, timeline_min.y + timeline_height/2),
		ImVec2(timeline_min.x + playhead_pos_x, timeline_max.y),
		ImGui::GetColorU32(playhead_color));

	if(mouse_hovering_timeline && ImGui::IsMouseClicked(0)) {
		lb_strokes_draggingPlayhead = true;
	} else if(ImGui::IsMouseReleased(0)) {
		lb_strokes_draggingPlayhead = false;
	}

	if(lb_strokes_draggingPlayhead) {
		lb_strokes_setTimelinePosition(ImGui::GetMousePos().x / io.DisplaySize.x * lb_strokes_timelineDuration);
	}

	struct lb_stroke* selected = lb_strokes_getSelectedStroke();
	if(selected) {
		float handle_l_x = selected->global_start_time / lb_strokes_timelineDuration * io.DisplaySize.x;
		float handle_r_x = (selected->global_start_time + selected->global_duration) / lb_strokes_timelineDuration * io.DisplaySize.x;
		ImVec2 handle_l = ImVec2(handle_l_x, timeline_min.y - 3);
		ImVec2 handle_r = ImVec2(handle_r_x, timeline_min.y - 3);

		draw_list->AddLine(ImVec2(handle_l.x, handle_l.y - 4), ImVec2(handle_r.x, handle_r.y - 4), ImGui::GetColorU32(ImGuiCol_TextDisabled));

		bool mouse_hovering_handle_l = ImGui::IsMouseHoveringRect(ImVec2(handle_l.x - 6, handle_l.y - 6), ImVec2(handle_l.x + 6, handle_l.y));
		bool mouse_hovering_handle_r = ImGui::IsMouseHoveringRect(ImVec2(handle_r.x - 6, handle_r.y - 6), ImVec2(handle_r.x + 6, handle_r.y));

		static bool dragging_handle_l = false;
		static bool dragging_handle_r = false;

		if(mouse_hovering_handle_l && ImGui::IsMouseClicked(0)) {
			dragging_handle_l = true;
		} else if(ImGui::IsMouseReleased(0)) {
			dragging_handle_l = false;
		}
		if(mouse_hovering_handle_r && ImGui::IsMouseClicked(0)) {
			dragging_handle_r = true;
		} else if(ImGui::IsMouseReleased(0)) {
			dragging_handle_r = false;
		}

		if(dragging_handle_l && ImGui::IsMouseDragging()) {
			selected->global_start_time = ImGui::GetMousePos().x / io.DisplaySize.x * lb_strokes_timelineDuration;
		} else if(dragging_handle_r && ImGui::IsMouseDragging()) {
			selected->global_duration = (ImGui::GetMousePos().x / io.DisplaySize.x * lb_strokes_timelineDuration) - selected->global_start_time;
		}

		draw_list->AddTriangleFilled(
			handle_l,
			ImVec2(handle_l.x - 6, handle_l.y - 6),
			ImVec2(handle_l.x + 6, handle_l.y - 6),
			ImGui::GetColorU32(mouse_hovering_handle_l || dragging_handle_l ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive));
		draw_list->AddTriangleFilled(
			handle_r,
			ImVec2(handle_r.x - 6, handle_r.y - 6),
			ImVec2(handle_r.x + 6, handle_r.y - 6),
			ImGui::GetColorU32(mouse_hovering_handle_r || dragging_handle_r ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive));
	}

	ImGui::End();

	ImGui::PopStyleVar();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	if(mouse_hovering_playhead || lb_strokes_draggingPlayhead) {
		// TODO: Calculate this better
		ImGui::SetNextWindowPos(ImVec2(playhead_pos_x - 5.0f - 25.0f, timeline_min.y - style.ItemInnerSpacing.y - 5.0f - 20.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
		ImGui::BeginTooltip();
		ImGui::Text("%.2fs", lb_strokes_timelinePosition);
		ImGui::EndTooltip();
		ImGui::PopStyleVar();
		ImGui::PopStyleVar();
	}
}

static void drawTools() {
	// ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::SetNextWindowSize(ImVec2(32, 64));
	ImGui::SetNextWindowPos(style.WindowPadding);
	ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

	// ImGui::

	ImGui::End();
}

static void drawStrokeProperties() {
	struct lb_stroke* selected = lb_strokes_getSelectedStroke();
	if(!selected) return;

	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	ImColor bg = style.Colors[ImGuiCol_WindowBg];
	bg.Value.w = 0.2f;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, bg.Value);
	ImGui::SetNextWindowSize(ImVec2(200, 300));
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 200 - 5, 5));
	ImGui::Begin("Stroke Properties", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

	ImGui::Text("Playback");
	// ImGui::Combo("##Playback", (int*)&selected->playback, "Realtime\0Linear\0\0");

	ImGui::End();
	ImGui::PopStyleColor();
}

EXTERN_C void lb_ui_render(int windowWidth, int windowHeight, int framebufferWidth, int framebufferHeight, double dt) {
	
	ImGuiIO& io = ImGui::GetIO();
	
	io.DisplaySize = ImVec2((float)windowWidth, (float)windowHeight);
	io.DisplayFramebufferScale = ImVec2(windowWidth > 0 ? ((float)framebufferWidth / windowWidth) : 0, windowHeight > 0 ? ((float)framebufferHeight / windowHeight) : 0);
	io.DeltaTime = dt;
	
	// Manage inputs
	// (we already got mouse wheel, keyboard keys & characters from glfw callbacks polled in glfwPollEvents())
	//if(windowFocused) {
		// if (io.WantMoveMouse) {
		// 	glfwSetCursorPos(g_Window, (double)io.MousePos.x, (double)io.MousePos.y);   // Set mouse position if requested by io.WantMoveMouse flag (used when io.NavMovesTrue is enabled by user and using directional navigation)
		// } else {
	//}
	
	if(windowFocused) {
		io.MousePos = ImVec2((float) lastMouseX, (float) lastMouseY);
	} else {
		io.MousePos = ImVec2(-FLT_MAX,-FLT_MAX);
	}
	
	io.MouseWheel = scrollAccumulator;
	scrollAccumulator = 0.0f;
	
	// Start the frame. This call will update the io.WantCaptureMouse, io.WantCaptureKeyboard flag that you can use to dispatch inputs (or not) to your application.
	ImGui::NewFrame();
	
	drawMainMenuBar();
	if(guiState.showDemoPanel) ImGui::ShowDemoWindow(&guiState.showDemoPanel);
	drawTools();
	drawTimeline();
	drawStrokeProperties();

	ImGui::Render();
}

EXTERN_C bool lb_ui_isDrawingCursor() {
	return ImGui::GetIO().MouseDrawCursor;
}

EXTERN_C bool lb_ui_capturedMouse() {
	return ImGui::GetIO().WantCaptureMouse;
}

EXTERN_C bool lb_ui_capturedKeyboard() {
	return ImGui::GetIO().WantCaptureKeyboard;
}