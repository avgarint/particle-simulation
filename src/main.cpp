/****************************************************************************\
 * Pilot Alex, 2022-2023, All right reserved. Copyright (c) 2023.           *
 * Made by A.G. under the username of Pilot Alex.                           *
 * C++17, Visual Studio 2022.                                               *
\****************************************************************************/

#include <map>
#include <tuple>
#include <array>
#include <cmath>
#include <string>
#include <random>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>
#include <iostream>

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui with std::string
#include "imgui_sdl_backend/imgui_impl_sdl2.h"
#include "imgui_sdl_backend/imgui_impl_sdlrenderer2.h"

#include "json/json.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#undef main

constexpr int CELL_SIZE = 10;
constexpr int WINDOW_HEIGHT = 800;
constexpr int WINDOW_WIDTH = 800;

constexpr std::string_view MATERIAL_NAME_NONE = "none";
constexpr std::string_view MATERIAL_FILE_PATH = "./materials.json";

// --------------------------------------------------------------------------------------------

enum MaterialType // TODO: should be enum class
{
    MATERIAL_TYPE_NONE,   // Used to represent an empty cell/particle
    MATERIAL_TYPE_SOLID,  // Solid materials such as sand, salt and more
    MATERIAL_TYPE_LIQUID, // Liquid materials such as water, lava and more
    MATERIAL_TYPE_GAS     // Gas materials such as toxic gas and more
};

enum BrushType // TODO: should be enum class
{
    BRUSH_TYPE_SMALL  = 1, // Reveal a single particle at once
    BRUSH_TYPE_MEDIUM = 8, // Reveal particles in located in a rect with an extent of 8
    BRUSH_TYPE_BIG    = 16 // Reveal particles in located in a rect with an extent of 16
};

// --------------------------------------------------------------------------------------------

static std::vector<std::string> brushOptions = { "Small (1px)", "Medium (extent = 8)", "Big (extent = 16)" };
static int selectedBrushOption = 0;

static std::vector<std::string> materialOptions = { std::string(MATERIAL_NAME_NONE) };
static int selectedMaterialOption = 0;

static BrushType brush = BRUSH_TYPE_SMALL;

// --------------------------------------------------------------------------------------------

// TODO: merge?
struct SpreadRules
{
    int spreadSpeed;
    std::vector<std::string> canReplace;
    std::map<std::string, SDL_Color> contactColors;
    std::map<std::string, std::string> contactSounds;
};

struct Material
{
    int type = MATERIAL_TYPE_NONE;
    std::string name = std::string(MATERIAL_NAME_NONE);
    SDL_Color color = { 0, 0, 0, 0 };
};

struct Particle
{
    float lifeTime = -1.0f;
    bool hasBeenUpdatedThisFrame = false;
    SpreadRules spreadRules;
    Material material;
};

struct CustomParticle
{
    std::string name;
    int type;
    float initialLifeTime;
    float initialColor[3];
    std::vector<std::string> canReplace;
    std::map<std::string, std::array<float, 3>> contactColors;
    std::map<std::string, std::string> contactSounds;
    int spreadSpeed;
};

// --------------------------------------------------------------------------------------------

typedef std::vector<std::unique_ptr<Particle>> Grid;

static CustomParticle p; // TODO: what's this?

// --------------------------------------------------------------------------------------------

std::default_random_engine rng;
std::random_device rd;
std::mt19937 gen(rd());

// Function to generate a random float between min and max (inclusive)
float RandomFloat(float min, float max)
{
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

// --------------------------------------------------------------------------------------------

// Returns a new rgb color as an SDL_Color struct that the particle p should
// take when in collides with the material named materialName.
SDL_Color GetParticleContactColor(Particle* p, const std::string& materialName)
{
    if (p)
    {
        const auto& contactColors = p->spreadRules.contactColors;

        auto it = contactColors.find(materialName);
        if (it != contactColors.end())
        {
            return it->second;
        }
    }

    return SDL_Color{ 0, 0, 0, 255 };
}

// --------------------------------------------------------------------------------------------

// Returns the index in the list of the cell located at x and y.
int GetCellIndex(int gridWidth, int x, int y)
{
    return y * gridWidth + x;
}

// --------------------------------------------------------------------------------------------

// Opens and reads the json file located at savePath and returns it.
nlohmann::json LoadMaterialJsonData(const std::string& savePath) // TODO: should be std::filesystem::path if C++17 or >
{
    std::ifstream file(savePath);

    if (!file.is_open())
    {
        std::cout << "Failed to open JSON file at: " << savePath << std::endl;
        return nlohmann::json::value_t::null;
    }

    try
    {
        nlohmann::json data = nlohmann::json::parse(file);

        // Build for material selection dropdown
        for (const auto& material : data)
        {
            std::string name = material["name"].get<std::string>();

            if (std::find(materialOptions.begin(), materialOptions.end(), name) == materialOptions.end())
            {
                materialOptions.push_back(name);
            }
        }

        return data;
    }

    catch (const std::exception& ex)
    {
        std::cout << "Failed to parse JSON: " << ex.what() << std::endl;
        return nlohmann::json::value_t::null;
    }
}

// --------------------------------------------------------------------------------------------

// Returns true if the particle p is allowed to spread and replace the material named materialName.
bool ParticleCanSpreadTo(Particle* p, const std::string& materialName)
{
    if (p)
    {
        const SpreadRules& spreadRules = p->spreadRules;
        const std::vector<std::string>& canReplace = spreadRules.canReplace;

        auto it = std::find(canReplace.begin(), canReplace.end(), materialName);

        if (it != canReplace.end())
        {
            return true;
        }
    }

    return false;
}

// Returns wether the cell located at x and y on the grid is empty or not.
bool CellIsEmpty(const Grid& cells, int gridWidth, int x, int y)
{
    int index = GetCellIndex(x, y, gridWidth);
    return cells[index]->material.name == MATERIAL_NAME_NONE;
}

// Returns wether the cell located at x and y on the grid is empty or not.
bool ParticleIsEmpty(Particle* p)
{
    return p->material.name == MATERIAL_NAME_NONE;
}

// Returns true on full success.
bool InitSDL(SDL_Window*& window, SDL_Renderer*& renderer)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        std::cout << "SDL initialization failed: " << SDL_GetError() << " " << Mix_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow("Particle simulation",
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               WINDOW_WIDTH,
                               WINDOW_HEIGHT,
                               SDL_WINDOW_SHOWN);

    if (!window)
    {
        std::cout << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!renderer)
    {
        std::cout << "Renderer creation failed: " << SDL_GetError() << std::endl;
        Mix_CloseAudio();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    return true;
}

// Returns true on full success.
bool InitImGui(SDL_Window* window, SDL_Renderer* renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer2_Init(renderer))
    {
        std::cout << "Error on ImGui SDL renderer init!" << std::endl;
        return false;
    }

    return true;
}

// --------------------------------------------------------------------------------------------

// Returns a rect based on the cell located at x and y on the grid.
SDL_Rect CellToRect(int x, int y, int cellSize)
{
    SDL_Rect ret;
    ret.x = x * cellSize;
    ret.y = y * cellSize;
    ret.w = cellSize;
    ret.h = cellSize;
    return ret;
}

// Transforms a mouse coordinates tuple to a rect bounds accordingly to the grid.
SDL_Rect MouseCoordinatesToBounds(int gridWidth, int gridHeight, int cellSize, int mouseX, int mouseY, int extent)
{
    int cellX = mouseX / cellSize;
    int cellY = mouseY / cellSize;

    int xStart = std::max(0, cellX - extent);
    int yStart = std::max(0, cellY - extent);
    int xEnd = std::min(gridWidth - 1, cellX + extent);
    int yEnd = std::min(gridHeight - 1, cellY + extent);

    return SDL_Rect{ xStart, yStart, xEnd, yEnd };
}

// --------------------------------------------------------------------------------------------

// Returns the particle located at x and y in the grid.
Particle* GetParticleAt(const Grid& cells, int gridWidth, int x, int y)
{
    int index = GetCellIndex(gridWidth, x, y);
    if (index >= 0 && index < cells.size())
    {
        return cells[index].get();
    }
    return nullptr;
}

// --------------------------------------------------------------------------------------------

// Transforms a mouse coordinates tuple to a row and column accordingly to the grid.
std::tuple<int, int> MouseCoordinatesToXY(int gridWidth, int gridHeight, int cellSize, int mouseX, int mouseY)
{
    int x = mouseX / cellSize;
    int y = mouseY / cellSize;

    // Clamp the coordinates within the valid range
    x = std::max(0, std::min(x, gridWidth - 1));
    y = std::max(0, std::min(y, gridHeight - 1));

    return std::make_tuple(x, y);
}

// --------------------------------------------------------------------------------------------

// Loads or updates the material of the particle from the json file.
void LoadParticleMaterial(Particle* p, const nlohmann::json& data)
{
    if (!p)
    {
        std::cout << "Invalid particle pointer." << std::endl;
        return;
    }

    for (const auto& material : data)
    {
        if (material["name"] == p->material.name)
        {
            p->material.name = material["name"];
            p->material.type = material["type"];
            p->lifeTime = material["initial_life_time"];
            p->material.color = { material["initial_color"][0], material["initial_color"][1], material["initial_color"][2] };

            p->spreadRules.canReplace = material["spread_rules"]["can_replace"].get<std::vector<std::string>>();

            const auto& contactColorsJson = material["spread_rules"]["contact_colors"];

            for (auto it = contactColorsJson.begin(); it != contactColorsJson.end(); ++it)
            {
                const std::string& key = it.key();
                const std::vector<float>& colorArray = it.value();
                SDL_Color color = { static_cast<Uint8>(colorArray[0]), static_cast<Uint8>(colorArray[1]), static_cast<Uint8>(colorArray[2]), 255 };
                p->spreadRules.contactColors[key] = color;
            }

            p->spreadRules.contactSounds = material["spread_rules"]["contact_sounds"].get<std::map<std::string, std::string>>();
            p->spreadRules.spreadSpeed = material["spread_rules"]["spread_speed"];
        }
    }
}

// Writes to the .json file located at savePath the data held by the particle p.
void SerializeParticle(const CustomParticle& p, const std::string& savePath)
{
    nlohmann::json newMaterial =
    {
            { "name", p.name },
            { "type", p.type },
            { "initial_life_time", p.initialLifeTime },
            { "initial_color", p.initialColor },
            { "spread_rules", {
                { "can_replace", p.canReplace },
                { "contact_colors", p.contactColors },
                { "contact_sounds", p.contactSounds },
                { "spread_speed", p.spreadSpeed }
            }}
    };

    std::ifstream file(savePath);
    nlohmann::json existingData;

    if (file.good())
    {
        file >> existingData;
        file.close();
    }

    if (!existingData.is_array())
    {
        existingData = nlohmann::json::array();
    }

    // Add the new material to the existing materials array
    existingData.push_back(newMaterial);

    // Write the updated JSON data back to the file with inline array formatting
    std::ofstream outFile(savePath);
    outFile << existingData.dump(2) << std::endl;
    outFile.close();
}

// Lights up a particle from the grid located at x and y.
void RevealParticleAt(const Grid& cells, int gridWidth, int x, int y, const std::string& materialName)
{
    Particle* spawnParticle = GetParticleAt(cells, gridWidth, x, y);

    // Refresh material
    spawnParticle->material.name = materialName;
    spawnParticle->material.type = MATERIAL_TYPE_SOLID;

    static nlohmann::json data;

    if (data.is_null())
    {
        data = LoadMaterialJsonData(std::string(MATERIAL_FILE_PATH));
    }

    LoadParticleMaterial(spawnParticle, data);
}

// Lights up particles from the grid located in the bounds.
void RevealParticlesAt(const Grid& cells, int gridWidth, const SDL_Rect& bounds, const std::string& materialName)
{
    int xStart = bounds.x;
    int yStart = bounds.y;
    int xEnd = bounds.w;
    int yEnd = bounds.h;

    int totalParticles = (xEnd - xStart + 1) * (yEnd - yStart + 1);

    // Percentage of particles to reveal in each iteration
    double revealPercentage = 0.2;
    int particlesToReveal = static_cast<int>(totalParticles * revealPercentage);

    int centerX = static_cast<int>(std::floor((xStart + xEnd) / 2));
    int centerY = static_cast<int>(std::floor((yStart + yEnd) / 2));

    for (int i = 0; i < particlesToReveal; ++i)
    {
        float angle = RandomFloat(0.0f, 2.0f * M_PI);
        float radius = RandomFloat(0.0f, std::min(centerX - xStart, centerY - yStart));
        int x = static_cast<int>(centerX + radius * std::cos(angle));
        int y = static_cast<int>(centerY + radius * std::sin(angle));

        // Ensure the generated coordinates are within the bounds
        x = std::max(xStart, std::min(x, xEnd));
        y = std::max(yStart, std::min(y, yEnd));

        RevealParticleAt(cells, gridWidth, x, y, materialName);
    }
}

// p1 becomes p2 and p2 becomes p1.
void SwapParticles(Particle& p1, Particle& p2)
{
    std::swap(p1.material.name, p2.material.name);
    std::swap(p1.material.type, p2.material.type);
    std::swap(p1.material.color, p2.material.color);
    std::swap(p1.spreadRules, p2.spreadRules);
}

// Blits the particle to the window. (unused)
void DrawParticle(SDL_Renderer* renderer, Particle* p, const SDL_Rect& rect)
{
    if (renderer && p)
    {
        SDL_RenderFillRect(renderer, &rect);
    }
}

// Updates the solid particle located at x and y on the grid.
void UpdateSolid(const Grid& cells, int gridWidth, int x, int y)
{
    Particle* solidParticle = GetParticleAt(cells, gridWidth, x, y);

    // Get neighboring particles
    Particle* bParticle = GetParticleAt(cells, gridWidth, x, y + 1); // Below
    Particle* blParticle = GetParticleAt(cells, gridWidth, x - 1, y + 1); // Below left
    Particle* brParticle = GetParticleAt(cells, gridWidth, x + 1, y + 1); // Below right

    if (bParticle && (ParticleIsEmpty(bParticle) || ParticleCanSpreadTo(solidParticle, bParticle->material.name))) // Move down
    {
        solidParticle->material.color = GetParticleContactColor(solidParticle, bParticle->material.name);
        SwapParticles(*bParticle, *solidParticle);
    }

    else if (blParticle && ParticleIsEmpty(blParticle)) // Move down and left
    {
        SwapParticles(*blParticle, *solidParticle);
    }

    else if (brParticle && ParticleIsEmpty(brParticle)) // Move down and right
    {
        SwapParticles(*brParticle, *solidParticle);
    }
}

// Updates the liquid particle located at x and y on the grid.
void UpdateLiquid(const Grid& cells, int gridWidth, int x, int y)
{
    Particle* liquidParticle = GetParticleAt(cells, gridWidth, x, y);

    // Get neighboring particles
    Particle* lParticle = GetParticleAt(cells, gridWidth, x - 1, y); // Left
    Particle* rParticle = GetParticleAt(cells, gridWidth, x + 1, y); // Right

    Particle* bParticle = GetParticleAt(cells, gridWidth, x, y + 1); // Below
    Particle* blParticle = GetParticleAt(cells, gridWidth, x - 1, y + 1); // Below left
    Particle* brParticle = GetParticleAt(cells, gridWidth, x + 1, y + 1); // Below right

    if (bParticle && (ParticleIsEmpty(bParticle) || ParticleCanSpreadTo(liquidParticle, bParticle->material.name))) // Move down
    {
        liquidParticle->material.color = GetParticleContactColor(liquidParticle, bParticle->material.name);
        SwapParticles(*bParticle, *liquidParticle);
    }

    else if (blParticle && ParticleIsEmpty(blParticle)) // Move down and left
    {
        SwapParticles(*blParticle, *liquidParticle);
    }

    else if (brParticle && ParticleIsEmpty(brParticle)) // Move down and right
    {
        SwapParticles(*brParticle, *liquidParticle);
    }

    else if (lParticle && ParticleIsEmpty(lParticle)) // Move left
    {
        SwapParticles(*lParticle, *liquidParticle);
    }

    else if (rParticle && ParticleIsEmpty(rParticle)) // Move right
    {
        SwapParticles(*rParticle, *liquidParticle);
    }
}

// Updates the gas particle located at x and y on the grid.
void UpdateGas(const Grid& cells, int gridWidth, int x, int y)
{
    Particle* gasParticle = GetParticleAt(cells, gridWidth, x, y);

    Particle* aParticle = GetParticleAt(cells, gridWidth, x, y - 1); // Above
    Particle* lParticle = GetParticleAt(cells, gridWidth, x - 1, y); // Left
    Particle* rParticle = GetParticleAt(cells, gridWidth, x + 1, y); // Right
    Particle* bParticle = GetParticleAt(cells, gridWidth, x, y + 1); // Below

    // Randomly select a direction to move
    std::vector<Particle*> directions = { aParticle, lParticle, rParticle, bParticle };
    std::shuffle(std::begin(directions), std::end(directions), rng);

    for (const auto& direction : directions)
    {
        if (direction && (ParticleIsEmpty(direction) || ParticleCanSpreadTo(gasParticle, direction->material.name)))
        {
            gasParticle->material.color = GetParticleContactColor(gasParticle, direction->material.name);
            SwapParticles(*direction, *gasParticle);
            break;
        }
    }
}

// Updates the inputs related the the material selection.
void UpdateInputs(const SDL_Event& event, const ImGuiIO& io, const Grid& cells, int gridWidth, int gridHeight)
{
    static bool mouseDown = false;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        mouseDown = true;
    }
    else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
    {
        mouseDown = false;
    }

    if (mouseDown && !io.WantCaptureMouse)
    {
        int mouseX;
        int mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        std::string filePath(MATERIAL_FILE_PATH);

        switch (brush)
        {
        case BRUSH_TYPE_SMALL:
        {
            auto coords = MouseCoordinatesToXY(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY);
            RevealParticleAt(cells, gridWidth, std::get<0>(coords), std::get<1>(coords), materialOptions[selectedMaterialOption]);
            break;
        }
        case BRUSH_TYPE_MEDIUM:
        case BRUSH_TYPE_BIG:
        {
            int brushSize = static_cast<std::underlying_type<BrushType>::type>(brush);
            SDL_Rect bounds = MouseCoordinatesToBounds(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY, brushSize);
            RevealParticlesAt(cells, gridWidth, bounds, materialOptions[selectedMaterialOption]);
            break;
        }
        default:
            break;
        }
    }
}

// Updates the particles motion.
void UpdateParticleSimulation(SDL_Renderer* renderer, const Grid& cells, int gridHeight, int gridWidth)
{
    for (int y = gridHeight - 1; y > 0; y--)
    {
        for (int x = 0; x < gridWidth; x++)
        {
            int matType = GetParticleAt(cells, gridWidth, x, y)->material.type;

            switch (matType)
            {
            case MATERIAL_TYPE_NONE:
                break;

            case MATERIAL_TYPE_SOLID:
                UpdateSolid(cells, gridWidth, x, y);
                break;

            case MATERIAL_TYPE_LIQUID:
                UpdateLiquid(cells, gridWidth, x, y);
                break;

            case MATERIAL_TYPE_GAS:
                UpdateGas(cells, gridWidth, x, y);
                break;

            default:
                break;
            }
        }
    }

    for (int columnIndex = 0; columnIndex < gridHeight; columnIndex++)
    {
        for (int rowIndex = 0; rowIndex < gridWidth; rowIndex++)
        {
            Particle* currentParticle = GetParticleAt(cells, gridWidth, rowIndex, columnIndex);
            SDL_Rect rect = CellToRect(rowIndex, columnIndex, CELL_SIZE);
            SDL_SetRenderDrawColor(renderer,
                                   currentParticle->material.color.r,
                                   currentParticle->material.color.g,
                                   currentParticle->material.color.b,
                                   currentParticle->material.color.a);
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

// Renders the UI related to the brush type selection.
void RenderBrushSelectionDropdown()
{
    if (ImGui::BeginCombo("Brush", brushOptions[selectedBrushOption].c_str()))
    {
        for (int i = 0; i < brushOptions.size(); i++)
        {
            bool isSelected = (selectedBrushOption == i);

            if (ImGui::Selectable(brushOptions[i].c_str(), isSelected))
            {
                selectedBrushOption = i;

                switch (selectedBrushOption)
                {
                case 0:
                    brush = BRUSH_TYPE_SMALL;
                    break;

                case 1:
                    brush = BRUSH_TYPE_MEDIUM;
                    break;

                case 2:
                    brush = BRUSH_TYPE_BIG;
                    break;

                default:
                    break;
                }

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }

        ImGui::EndCombo();
    }
}

// Renders the UI related to the material selection.
void RenderMaterialSelectionDropdown()
{
    if (ImGui::BeginCombo("Material", materialOptions[selectedMaterialOption].c_str()))
    {
        for (int i = 0; i < materialOptions.size(); i++)
        {
            bool isSelected = (selectedMaterialOption == i);

            if (ImGui::Selectable(materialOptions[i].c_str(), isSelected))
            {
                selectedMaterialOption = i;

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }

        ImGui::EndCombo();
    }
}

// Render the UI related to the static elements of the custom particle creation such
// as the name, type...
void RenderStaticSection()
{
    static float initialColor[3];

    ImGui::InputText("Name:", &p.name); // Name
    ImGui::InputInt("Type (1 = solid, 2 = liquid, 3 = gas):", &p.type); // Type
    ImGui::InputFloat("Initial life time:", &p.initialLifeTime); // Life time

    if (ImGui::ColorPicker3("Initial color:", initialColor))
    {
        for (int i = 0; i < std::size(initialColor); i++)
        {
            p.initialColor[i] = initialColor[i] * 255;
        }
    }

    ImGui::Text("Spread rules");
    ImGui::Text("Can replace:"); // Can replace

    for (int i = 0; i < p.canReplace.size(); i++)
    {
        std::string inputId = "##canreplace" + std::to_string(i);
        ImGui::InputText(inputId.c_str(), &p.canReplace[i]);
    }

    if (ImGui::SmallButton("Add new replacement entry"))
    {
        p.canReplace.emplace_back();
    }

    ImGui::InputInt("Spread speed:", &p.spreadSpeed); // Spread speed
}

// Renders the UI related to the contact color of the custom particle.
void RenderContactColorsSection()
{
    ImGui::Text("Contact colors:");

    auto colorIt = p.contactColors.begin();
    size_t colorIndex = 0;

    static float color[3];

    for (; colorIt != p.contactColors.end();)
    {
        std::string oldKey = colorIt->first;
        std::array<float, 3> value = colorIt->second;

        std::string keyID = "##ckey" + std::to_string(colorIndex);
        std::string valueID = "##cvalue" + std::to_string(colorIndex);

        ImGui::PushItemWidth(250.0f);
        ImGui::InputText(keyID.c_str(), &oldKey);
        ImGui::PushItemWidth(250.0f);
        ImGui::ColorPicker3(valueID.c_str(), color);

        if (oldKey != colorIt->first)
        {
            // Key has changed, remove the old entry and insert a new one
            auto it = p.contactColors.extract(colorIt++);
            it.key() = oldKey;
            p.contactColors.insert(std::move(it));
        }

        else
        {
            // Update the value of the existing entry
            colorIt->second = value;
            ++colorIt;
        }

        ++colorIndex; // Increment the counter for the next iteration
    }

    if (ImGui::SmallButton("Add new contact color entry"))
    {
        p.contactColors.emplace("", std::array<float, 3>{0, 0, 0});
    }
}

// Renders the UI related to the contact sounds of the custom particle.
void RenderContactSoundsSection()
{
    ImGui::Text("Contact sounds:");

    auto soundIt = p.contactSounds.begin();
    size_t soundIndex = 0;

    for (; soundIt != p.contactSounds.end();)
    {
        std::string oldKey = soundIt->first;
        std::string value = soundIt->second;

        std::string keyID = "##skey" + std::to_string(soundIndex);
        std::string valueID = "##svalue" + std::to_string(soundIndex);

        ImGui::InputText(keyID.c_str(), &oldKey);
        ImGui::SameLine();
        ImGui::InputText(valueID.c_str(), &value);

        if (oldKey != soundIt->first)
        {
            // Key has changed, remove the old entry and insert a new one
            auto it = p.contactSounds.extract(soundIt++);
            it.key() = oldKey;
            p.contactSounds.insert(std::move(it));
        }
        else
        {
            // Update the value of the existing entry
            soundIt->second = value;
            ++soundIt;
        }

        ++soundIndex; // Increment the counter for the next iteration
    }

    if (ImGui::SmallButton("Add new contact sound entry"))
    {
        p.contactSounds.emplace("", "");
    }
}

// Renders the entire UI in one same call.
void OnImGuiRenderAll()
{
    ImGui::Begin("Panel");

    ImGui::BeginTabBar("tab_bar");

    if (ImGui::BeginTabItem("Controls"))
    {
        RenderBrushSelectionDropdown();
        RenderMaterialSelectionDropdown();
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Custom material editor"))
    {
        RenderStaticSection();
        RenderContactColorsSection();
        RenderContactSoundsSection();

        if (ImGui::Button("Save material to file"))
        {
            SerializeParticle(p, std::string(MATERIAL_FILE_PATH));
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    ImGui::End();
}

// Destroy ImGui and SDL related elements.
void Shutdown(SDL_Window* window, SDL_Renderer* renderer)
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// --------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    bool shouldQuit = false;

    if (!InitSDL(window, renderer) || !InitImGui(window, renderer))
    {
        std::cout << "Error on SDL or ImGui init!" << std::endl;
        return -1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    Grid cells;
    int width = static_cast<int>(std::floor(WINDOW_WIDTH / CELL_SIZE));
    int height = static_cast<int>(std::floor(WINDOW_HEIGHT / CELL_SIZE));

    for (int i = 0; i < width * height; i++)
    {
        std::unique_ptr<Particle> newCell = std::make_unique<Particle>();

        newCell->material.name = MATERIAL_NAME_NONE;
        newCell->material.type = MATERIAL_TYPE_NONE;
        cells.push_back(std::move(newCell));
    }

    ImGuiIO& io = ImGui::GetIO();

    // Game loop
    while (!shouldQuit)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            UpdateInputs(event, io, cells, width, height);

            if (event.type == SDL_QUIT)
            {
                shouldQuit = true;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();

        OnImGuiRenderAll();

        ImGui::Render();

        SDL_RenderClear(renderer);
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

        UpdateParticleSimulation(renderer, cells, height, width);

        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    cells.clear();

    Shutdown(window, renderer);

    return 0;
}
