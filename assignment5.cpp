#include "assignment5.hpp"
#include "interpolation.hpp"

#include "parametric_shapes.hpp"

#include "config.hpp"
#include "core/Bonobo.h"
#include "core/FPSCamera.h"
#include "core/helpers.hpp"
#include "core/node.hpp"
#include "core/ShaderProgramManager.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tinyfiledialogs.h>
#include <clocale>
#include <cstdlib>

#include <stdexcept>

edaf80::Assignment5::Assignment5(WindowManager& windowManager) :
	mCamera(0.5f * glm::half_pi<float>(),
		static_cast<float>(config::resolution_x) / static_cast<float>(config::resolution_y),
		0.01f, 1000.0f),
	inputHandler(), mWindowManager(windowManager), window(nullptr)
{
	WindowManager::WindowDatum window_datum{ inputHandler, mCamera, config::resolution_x, config::resolution_y, 0, 0, 0, 0 };

	window = mWindowManager.CreateGLFWWindow("EDAF80: Assignment 5", window_datum, config::msaa_rate);
	if (window == nullptr) {
		throw std::runtime_error("Failed to get a window: aborting!");
	}

	bonobo::init();
}

edaf80::Assignment5::~Assignment5()
{
	bonobo::deinit();
}

void
edaf80::Assignment5::run()
{
	// Set up the camera
	mCamera.mWorld.SetTranslate(glm::vec3(0.0f, 0.0f, 6.0f));
	mCamera.mMouseSensitivity = 0.003f;
	mCamera.mMovementSpeed = 3.0f; // 3 m/s => 10.8 km/h


	// Create the shader programs
	ShaderProgramManager program_manager;
	GLuint fallback_shader = 0u;
	program_manager.CreateAndRegisterProgram("Fallback",
		{ { ShaderType::vertex, "common/fallback.vert" },
		  { ShaderType::fragment, "common/fallback.frag" } },
		fallback_shader);

	if (fallback_shader == 0u) {
		LogError("Failed to load fallback shader");
		return;
	}

	GLuint diffuse_shader = 0u;
	program_manager.CreateAndRegisterProgram("Diffuse",
		{ { ShaderType::vertex, "EDAF80/diffuse.vert" },
		  { ShaderType::fragment, "EDAF80/diffuse.frag" } },
		diffuse_shader);
	if (diffuse_shader == 0u)
		LogError("Failed to load diffuse shader");

	GLuint normal_shader = 0u;
	program_manager.CreateAndRegisterProgram("Normal",
		{ { ShaderType::vertex, "EDAF80/normal.vert" },
		  { ShaderType::fragment, "EDAF80/normal.frag" } },
		normal_shader);
	if (normal_shader == 0u)
		LogError("Failed to load normal shader");

	GLuint texcoord_shader = 0u;
	program_manager.CreateAndRegisterProgram("Texture coords",
		{ { ShaderType::vertex, "EDAF80/texcoord.vert" },
		  { ShaderType::fragment, "EDAF80/texcoord.frag" } },
		texcoord_shader);
	if (texcoord_shader == 0u)
		LogError("Failed to load texcoord shader");



	GLuint Skybox_shader = 0u;
	program_manager.CreateAndRegisterProgram("skybox",
		{ { ShaderType::vertex, "EDAF80/skybox.vert" },
		  { ShaderType::fragment, "EDAF80/skybox.frag" } },
		Skybox_shader);
	if (Skybox_shader == 0u)
		LogError("Failed to load skybox shader");

	GLuint phong_shader = 0u;
	program_manager.CreateAndRegisterProgram("phong",
		{ { ShaderType::vertex, "EDAF80/phong.vert" },
		  { ShaderType::fragment, "EDAF80/phong.frag" } },
		phong_shader);
	if (phong_shader == 0u)
		LogError("Failed to load phong shader");



	auto light_position = glm::vec3(-2.0f, 4.0f, 2.0f);
	auto const set_uniforms = [&light_position](GLuint program) {
		glUniform3fv(glGetUniformLocation(program, "light_position"), 1, glm::value_ptr(light_position));
	};

	bool use_normal_mapping = false;
	auto camera_position = mCamera.mWorld.GetTranslation();
	auto ship_position = camera_position + mCamera.mWorld.GetFront() * 0.015f + glm::vec3(0.0f, -0.003f, 0.0f);
	auto ambient = glm::vec3(0.1f, 0.1f, 0.1f);
	auto diffuse = glm::vec3(0.7f, 0.2f, 0.4f);
	auto specular = glm::vec3(1.0f, 1.0f, 1.0f);
	auto shininess = 10.0f;
	auto const phong_set_uniforms = [&use_normal_mapping, &light_position, &camera_position, &ambient, &diffuse, &specular, &shininess](GLuint program) {
		glUniform1i(glGetUniformLocation(program, "use_normal_mapping"), use_normal_mapping ? 1 : 0);
		glUniform3fv(glGetUniformLocation(program, "light_position"), 1, glm::value_ptr(light_position));
		glUniform3fv(glGetUniformLocation(program, "camera_position"), 1, glm::value_ptr(camera_position));
		glUniform3fv(glGetUniformLocation(program, "ambient"), 1, glm::value_ptr(ambient));
		glUniform3fv(glGetUniformLocation(program, "diffuse"), 1, glm::value_ptr(diffuse));
		glUniform3fv(glGetUniformLocation(program, "specular"), 1, glm::value_ptr(specular));
		glUniform1f(glGetUniformLocation(program, "shininess"), shininess);
	};

	std::string texture_path = config::resources_path("textures/");
	auto demo_diffuse_texture = bonobo::loadTexture2D(texture_path + "cobblestone_floor_08_diff_2k.jpg");
	auto demo_specular_map = bonobo::loadTexture2D(texture_path + "cobblestone_floor_08_rough_2k.jpg");
	auto demo_normal_map = bonobo::loadTexture2D(texture_path + "cobblestone_floor_08_nor_2k.jpg");

	//
	// Set up the two spheres used.
	//
	auto skybox_shape = parametric_shapes::createSphere(200.0f, 100u, 100u);
	if (skybox_shape.vao == 0u) {
		LogError("Failed to retrieve the mesh for the skybox");
		return;
	}

	auto torus_shape = parametric_shapes::createTorus(2, 1, 100, 100);
	if (torus_shape.vao == 0u)
	{
		LogError("Failed to retrive mesh for torus");
	}

	auto paper_plane_shape = bonobo::loadObjects(config::resources_path("models/paper_airplane.obj"));

	if (paper_plane_shape.empty())
	{
		LogError("failed to load the plane");
		return;
	}

	Node ship;
	auto const& plane_front = paper_plane_shape.front();

	Node skybox;
	Node Tori[9];



	auto skybox_cubemap_id = bonobo::loadTextureCubeMap(config::resources_path("cubemaps/LarnacaCastle/posx.jpg"),
		config::resources_path("cubemaps/LarnacaCastle/negx.jpg"),
		config::resources_path("cubemaps/LarnacaCastle/posy.jpg"),
		config::resources_path("cubemaps/LarnacaCastle/negy.jpg"),
		config::resources_path("cubemaps/LarnacaCastle/posz.jpg"),
		config::resources_path("cubemaps/LarnacaCastle/negz.jpg"));

	skybox.set_geometry(skybox_shape);
	skybox.set_program(&Skybox_shader, set_uniforms);
	skybox.add_texture("skybox_cube_map", skybox_cubemap_id, GL_TEXTURE_CUBE_MAP);

	ship.set_geometry(parametric_shapes::createSphere(0.0005f, 10u, 10u));
	ship.set_program(&phong_shader, phong_set_uniforms);
	//ship.get_transform().Scale(0.2f);
	//ship.set_program(&fallback_shader, set_uniforms);
	ship.add_texture("diffuse_texture", demo_diffuse_texture, GL_TEXTURE_2D);
	ship.add_texture("specular_texture", demo_specular_map, GL_TEXTURE_2D);
	ship.add_texture("normal_map", demo_normal_map, GL_TEXTURE_2D);

	std::array<glm::vec3, 9> control_point_locations = {
	glm::vec3(1.0f,  1.8f,  2.0f),
	glm::vec3(0.0f,  0.0f,  -12.0f),
	glm::vec3(1.0f,  1.8f,  -22.0f),
	glm::vec3(2.0f,  0.0f,  -32.0f),
	glm::vec3(1.0f,  1.8f,  -42.0f),
	glm::vec3(-0.5f, 0.0f,  -52.0f),
	glm::vec3(-3.0f, 1.8f, -62.0f),
	glm::vec3(-1.0f, 0.0f, -72.0f),
	glm::vec3(0.0f, 1.8f, -82.0f)
	};

	for (int i = 0; i < 9; i++)
	{
		Tori[i].set_geometry(torus_shape);
		Tori[i].set_program(&normal_shader, set_uniforms);
		Tori[i].get_transform().SetTranslate(control_point_locations[i]);
		Tori[i].get_transform().RotateX(glm::half_pi<float>());
	}

	auto demo_shape = parametric_shapes::createSphere(1.5f, 40u, 40u);
	if (demo_shape.vao == 0u) {
		LogError("Failed to retrieve the mesh for the demo sphere");
		return;
	}




	glClearDepthf(1.0f);
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glEnable(GL_DEPTH_TEST);


	auto lastTime = std::chrono::high_resolution_clock::now();

	std::int32_t demo_sphere_program_index = 0;
	auto cull_mode = bonobo::cull_mode_t::disabled;
	auto polygon_mode = bonobo::polygon_mode_t::fill;
	bool show_logs = true;
	bool show_gui = true;
	bool shader_reload_failed = false;
	bool show_basis = false;
	float basis_thickness_scale = 1.0f;
	float basis_length_scale = 1.0f;

	bool game_over = false;

	changeCullMode(cull_mode);

	while (!glfwWindowShouldClose(window)) {
		while (!game_over) {
			auto const nowTime = std::chrono::high_resolution_clock::now();
			auto const deltaTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(nowTime - lastTime);
			lastTime = nowTime;

			auto& io = ImGui::GetIO();
			inputHandler.SetUICapture(io.WantCaptureMouse, io.WantCaptureKeyboard);

			glfwPollEvents();
			inputHandler.Advance();
			mCamera.Update(deltaTimeUs, inputHandler);
			ship.get_transform().Translate(ship.get_transform().GetFront() * 0.05f);
			ship_position = ship.get_transform().GetTranslation();
			camera_position = ship_position + ship.get_transform().GetFront() * (-0.015f);

			//collision

			for (int i = 0; i < 9; i++)
			{
				//std::cout << "ship" << ship_position << std::endl;
				//std::cout << "torus" << control_point_locations[i] << std::endl;
				if (abs(ship_position.z - control_point_locations[i].z) < 0.1f) {
					//std::cout << "ship" << ship_position << std::endl;
					//std::cout << "torus" << control_point_locations[i] << std::endl;
					if (abs(ship_position.x - control_point_locations[i].x) >= 1 || abs(ship_position.y - control_point_locations[i].y) >= 1) {
						//game over
						game_over = true;
						std::cout << "Game over" << std::endl;
					}
				}
			}

			mCamera.mWorld.SetTranslate(camera_position);
			/*mCamera.mRotation.x = -ship.get_transform().GetFront().x;
			mCamera.mRotation.y = ship.get_transform().GetFront().y;
			mCamera.mWorld.SetRotateX(mCamera.mRotation.y);
			mCamera.mWorld.RotateY(mCamera.mRotation.x);*/

			mCamera.mWorld.LookTowards(ship_position - camera_position);

			//std::cout <<"ship"<< ship.get_transform().GetFront() << std::endl;
			//std::cout <<"camera"<< mCamera.mWorld.GetFront() << std::endl;
			//std::cout << "ship" << ship.get_transform().GetTranslation()- mCamera.mWorld.GetTranslation() << std::endl;
			//std::cout << "ship_pos" << ship.get_transform().GetTranslation() << std::endl;
			//std::cout << "camera_pos" << mCamera.mWorld.GetTranslation() << std::endl;
			//std::cout << glm::distance(camera_position, ship_position)<<std::endl;

			if (inputHandler.GetKeycodeState(GLFW_KEY_R) & JUST_PRESSED) {
				shader_reload_failed = !program_manager.ReloadAllPrograms();
				if (shader_reload_failed)
					tinyfd_notifyPopup("Shader Program Reload Error",
						"An error occurred while reloading shader programs; see the logs for details.\n"
						"Rendering is suspended until the issue is solved. Once fixed, just reload the shaders again.",
						"error");
			}
			if (inputHandler.GetKeycodeState(GLFW_KEY_F3) & JUST_RELEASED)
				show_logs = !show_logs;
			if (inputHandler.GetKeycodeState(GLFW_KEY_F2) & JUST_RELEASED)
				show_gui = !show_gui;
			if (inputHandler.GetKeycodeState(GLFW_KEY_F11) & JUST_RELEASED)
				mWindowManager.ToggleFullscreenStatusForWindow(window);


			int framebuffer_width, framebuffer_height;
			glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
			glViewport(0, 0, framebuffer_width, framebuffer_height);




			if (inputHandler.GetKeycodeState(GLFW_KEY_UP) & PRESSED) {
				ship.get_transform().RotateX(0.005);

			}

			if (inputHandler.GetKeycodeState(GLFW_KEY_DOWN) & PRESSED) {
				ship.get_transform().RotateX(-0.005);

			}

			if (inputHandler.GetKeycodeState(GLFW_KEY_LEFT) & PRESSED) {
				ship.get_transform().RotateY(0.005);
			}

			if (inputHandler.GetKeycodeState(GLFW_KEY_RIGHT) & PRESSED) {
				ship.get_transform().RotateY(-0.005);
			}



			mWindowManager.NewImGuiFrame();

			glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
			bonobo::changePolygonMode(polygon_mode);

			skybox.get_transform().SetTranslate(camera_position);
			skybox.render(mCamera.GetWorldToClipMatrix());
			for (int i = 0; i < 9; i++)
			{
				Tori[i].render(mCamera.GetWorldToClipMatrix());
			}

			ship.get_transform().SetTranslate(ship_position);
			ship.render(mCamera.GetWorldToClipMatrix());



			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

			bool opened = ImGui::Begin("Scene Control", nullptr, ImGuiWindowFlags_None);
			if (opened) {
				auto const cull_mode_changed = bonobo::uiSelectCullMode("Cull mode", cull_mode);
				if (cull_mode_changed) {
					changeCullMode(cull_mode);
				}
				bonobo::uiSelectPolygonMode("Polygon mode", polygon_mode);
				auto demo_sphere_selection_result = program_manager.SelectProgram("Demo sphere", demo_sphere_program_index);
				/*ImGui::Separator();
				ImGui::Checkbox("Use normal mapping", &use_normal_mapping);
				ImGui::ColorEdit3("Ambient", glm::value_ptr(ambient));
				ImGui::ColorEdit3("Diffuse", glm::value_ptr(diffuse));
				ImGui::ColorEdit3("Specular", glm::value_ptr(specular));
				ImGui::SliderFloat("Shininess", &shininess, 1.0f, 1000.0f);
				ImGui::SliderFloat3("Light Position", glm::value_ptr(light_position), -20.0f, 20.0f);
				ImGui::Separator();
				ImGui::Checkbox("Show basis", &show_basis);
				ImGui::SliderFloat("Basis thickness scale", &basis_thickness_scale, 0.0f, 100.0f);
				ImGui::SliderFloat("Basis length scale", &basis_length_scale, 0.0f, 100.0f);*/
				if (game_over) {
					ImGui::Text("Game over!!!");
				}
			}
			ImGui::End();

			if (show_basis)
				bonobo::renderBasis(basis_thickness_scale, basis_length_scale, mCamera.GetWorldToClipMatrix());

			opened = ImGui::Begin("Render Time", nullptr, ImGuiWindowFlags_None);
			if (opened)
				ImGui::Text("%.3f ms", std::chrono::duration<float, std::milli>(deltaTimeUs).count());
			ImGui::End();

			if (show_logs)
				Log::View::Render();
			mWindowManager.RenderImGuiFrame(show_gui);

			glfwSwapBuffers(window);
		}
	}
}

int main()
{
	std::setlocale(LC_ALL, "");

	Bonobo framework;

	try {
		edaf80::Assignment5 assignment5(framework.GetWindowManager());
		assignment5.run();
	}
	catch (std::runtime_error const& e) {
		LogError(e.what());
	}
}
