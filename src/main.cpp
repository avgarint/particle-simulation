/****************************************************************************\
 * Pilot Alex, 2022-2023, All right reserved. Copyright (c) 2023.           *
 * Made by A.G. under the username of Pilot Alex.                           *
\****************************************************************************/

#include <map>
#include <tuple>
#include <cmath>
#include <random>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>
#include <iostream>

#include <imgui.h>
#include "imgui_sdl_backend/imgui_impl_sdl2.h"
#include "imgui_sdl_backend/imgui_impl_sdlrenderer2.h"

#include "json/json.hpp"

#include <SDL.h>

#define CELL_SIZE 10

#define WINDOW_HEIGHT 800
#define WINDOW_WIDTH  800

#define MATERIAL_FILE_PATH "./materials.json"

#define MATERIAL_NAME_NONE     "none"
#define MATERIAL_NAME_SAND     "sand"
#define MATERIAL_NAME_WATER    "water"
#define MATERIAL_NAME_LAVA     "lava"
#define MATERIAL_NAME_ACID     "acid"
#define MATERIAL_NAME_TOXICGAS "toxicgas"

// --------------------------------------------------------------------------------------------

enum MaterialType
{
    MATERIAL_TYPE_NONE   = 0, // Used to represent an empty cell/particle
    MATERIAL_TYPE_SOLID  = 1, // Solid materials such as sand, salt and more
    MATERIAL_TYPE_LIQUID = 2, // Liquid materials such as water, lava and more
    MATERIAL_TYPE_GAS    = 3  // Gas materials such as toxic gas and more
};

enum BrushType
{
    BRUSH_TYPE_SMALL  = 1, // Reveal a single particle at once
    BRUSH_TYPE_MEDIUM = 8, // Reveal particles in located in a rect with an extent of 8
    BRUSH_TYPE_BIG    = 16 // Reveal particles in located in a rect with an extent of 16
};

// --------------------------------------------------------------------------------------------

static bool mouseDown = false;
static const char* selectedMaterial = MATERIAL_NAME_NONE;
static BrushType brush = BRUSH_TYPE_SMALL;

// --------------------------------------------------------------------------------------------

struct SpreadRules
{
    int spreadSpeed = 0;
    std::vector<std::string> canReplace;
    std::map<std::string, std::string> contactColors;
};

struct Material
{
    int type = MATERIAL_TYPE_NONE;
    std::string name = MATERIAL_NAME_NONE;
    SDL_Color color = { 0, 0, 0, 0 };
};

struct Particle
{
    float lifeTime = -1.0f;
    bool hasBeenUpdatedThisFrame = false;
    SpreadRules spreadRules;
    Material material;
};

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

// Converts and returns a string in a tuple like format made of three values 
// (r, b, b) to a SDL_Color struct. The alpha channel is set to 255.
SDL_Color ColorStringToSDLColor(const std::string& colorString)
{
    std::istringstream iss(colorString);
    char discard;
    int r, g, b;
    iss >> discard >> r >> discard >> g >> discard >> b >> discard;

    SDL_Color color;
    color.r = static_cast<Uint8>(r);
    color.g = static_cast<Uint8>(g);
    color.b = static_cast<Uint8>(b);
    color.a = 255;

    return color;
}

// Returns a new rgb color as an SDL_Color struct that the particle p should
// take when in contact with the material named materialName.
SDL_Color GetParticleContactColor(Particle* p, const std::string& materialName)
{
    if (p)
    {
        const auto& contactColors = p->spreadRules.contactColors;

        auto it = contactColors.find(materialName);
        if (it != contactColors.end())
        {
            return ColorStringToSDLColor(it->second);
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

// Loads or updates the material of the particle from the json file.
bool LoadParticleMaterial(Particle* p, const std::string& jsonPath)
{
    if (!p)
    {
        std::cout << "Invalid particle pointer." << std::endl;
        return false;
    }

    std::ifstream file(jsonPath);
    if (!file.is_open())
    {
        std::cout << "Failed to open JSON file: " << jsonPath << std::endl;
        return false;
    }

    try
    {
        nlohmann::json data = nlohmann::json::parse(file);

        for (const auto& material : data)
        {
            if (material["name"] == p->material.name)
            {
                p->material.name = material["name"];
                p->material.type = material["type"];
                p->lifeTime = material["initial_life_time"];
                p->material.color = ColorStringToSDLColor(material["initial_color"]);
                p->spreadRules.canReplace = material["spread_rules"]["can_replace"].get<std::vector<std::string>>();
                p->spreadRules.contactColors = material["spread_rules"]["contact_colors"].get<std::map<std::string, std::string>>();
                p->spreadRules.spreadSpeed = material["spread_rules"]["spread_speed"];
                return true;
            }
        }

        return false; // true?
    }

    catch (const std::exception& ex)
    {
        std::cout << "Failed to parse JSON: " << ex.what() << std::endl;
        return false;
    }
}

// Returns true if the particle p is allowed to spread and replace the material
// named materialName.
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
bool CellIsEmpty(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int x, int y)
{
    int index = GetCellIndex(x, y, gridWidth);
    return cells[index]->material.name == MATERIAL_NAME_NONE;
}

// Returns wether the cell located at x and y on the grid is empty or not.
bool ParticleIsEmpty(Particle* p)
{
    return p->material.name == MATERIAL_NAME_NONE;
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
Particle* GetParticleAt(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int x, int y)
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

// Lights up a particle from the grid located at x and y.
void RevealParticleAt(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int x, int y, const std::string& materialName, const std::string& jsonPath)
{
    Particle* spawnParticle = GetParticleAt(cells, gridWidth, x, y);

    // Refresh material
    spawnParticle->material.name = materialName;
    spawnParticle->material.type = MATERIAL_TYPE_SOLID;
    LoadParticleMaterial(spawnParticle, jsonPath);
}

// Lights up particles from the grid located in the bounds.
void RevealParticlesAt(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, const SDL_Rect& bounds, const std::string& materialName, const std::string& jsonPath)
{
    int xStart = bounds.x;
    int yStart = bounds.y;
    int xEnd = bounds.w;
    int yEnd = bounds.h;

    int totalParticles = (xEnd - xStart + 1) * (yEnd - yStart + 1);

    // Percentage of particles to reveal in each iteration
    float revealPercentage = 0.2;
    int particlesToReveal = static_cast<int>(totalParticles * revealPercentage);

    int centerX = std::floor((xStart + xEnd) / 2);
    int centerY = std::floor((yStart + yEnd) / 2);

    for (int i = 0; i < particlesToReveal; ++i)
    {
        float angle = RandomFloat(0.0, 2.0 * M_PI);
        float radius = RandomFloat(0.0, std::min(centerX - xStart, centerY - yStart));
        int x = static_cast<int>(centerX + radius * std::cos(angle));
        int y = static_cast<int>(centerY + radius * std::sin(angle));

        // Ensure the generated coordinates are within the bounds
        x = std::max(xStart, std::min(x, xEnd));
        y = std::max(yStart, std::min(y, yEnd));

        RevealParticleAt(cells, gridWidth, x, y, materialName, jsonPath);
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
void UpdateSolid(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int x, int y)
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
void UpdateLiquid(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int x, int y)
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
void UpdateGas(const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int x, int y)
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
void UpdateInputs(const SDL_Event& event, const ImGuiIO& io, const std::vector<std::unique_ptr<Particle>>& cells, int gridWidth, int gridHeight)
{
    if (event.type == SDL_MOUSEBUTTONDOWN)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            mouseDown = true;
        }
    }

    else if (event.type == SDL_MOUSEBUTTONUP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            mouseDown = false;
        }
    }

    if (mouseDown && !io.WantCaptureMouse)
    {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);

        std::string mat = selectedMaterial;

        switch (brush)
        {
        case BRUSH_TYPE_SMALL:
        {
            auto coords = MouseCoordinatesToXY(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY);
            RevealParticleAt(cells, gridWidth, std::get<0>(coords), std::get<1>(coords), mat, MATERIAL_FILE_PATH);
            break;
        }

        case BRUSH_TYPE_MEDIUM:
        {
            int brushSize = static_cast<std::underlying_type<BrushType>::type>(brush);
            SDL_Rect bounds = MouseCoordinatesToBounds(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY, brushSize);
            RevealParticlesAt(cells, gridWidth, bounds, mat, MATERIAL_FILE_PATH);
            break;
        }

        case BRUSH_TYPE_BIG:
        {
            int brushSize = static_cast<std::underlying_type<BrushType>::type>(brush);
            SDL_Rect bounds = MouseCoordinatesToBounds(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY, brushSize);
            RevealParticlesAt(cells, gridWidth, bounds, mat, MATERIAL_FILE_PATH);
            break;
        }

        default:
            break;
        }
    }
}

// Updates the particles motion.
void UpdateParticleSimulation(SDL_Renderer* renderer, const std::vector<std::unique_ptr<Particle>>& cells, int gridHeight, int gridWidth)
{
    for (int y = gridHeight - 1; y > 0; y--)
    {
        for (int x = 0; x < gridWidth; x++)
        {
            // todo: check for nullptr
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

            // Todo: this is slow I know I'll think about optimization later
            SDL_SetRenderDrawColor(renderer, 
                                   currentParticle->material.color.r, 
                                   currentParticle->material.color.g, 
                                   currentParticle->material.color.b, 
                                   currentParticle->material.color.a);
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

// Renders the brush size selection dropdown.
void OnImGuiRenderBrushDropDown()
{
    static constexpr const char* brushes[] =
    {
        "brush small",
        "brush medium",
        "brush big"
    };

    const char* selectedBrushSize = brushes[0];

    ImGui::Begin("Parameters");

    if (ImGui::BeginCombo("##brush_selection", selectedBrushSize))
    {
        for (int n = 0; n < IM_ARRAYSIZE(brushes); n++)
        {
            bool isSelected = (selectedBrushSize == brushes[n]);

            if (ImGui::Selectable(brushes[n], isSelected))
            {
                selectedBrushSize = brushes[n];

                if (selectedBrushSize == brushes[0])
                { 
                    brush = BRUSH_TYPE_SMALL; 
                }

                else if (selectedBrushSize == brushes[1])
                {
                    brush = BRUSH_TYPE_MEDIUM;
                }

                else if (selectedBrushSize == brushes[2])
                { 
                    brush = BRUSH_TYPE_BIG;
                }

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }

        ImGui::EndCombo();
    }

    ImGui::End();
}

// Renders the material selection dropdown.
void OnImGuiRenderMaterialDropdown()
{
    // TODO: build this from json
    static constexpr const char* materials[] = 
    { 
        MATERIAL_NAME_NONE,
        MATERIAL_NAME_SAND,
        MATERIAL_NAME_WATER,
        MATERIAL_NAME_LAVA,
        MATERIAL_NAME_ACID,
        MATERIAL_NAME_TOXICGAS
    };

    ImGui::Begin("Parameters");

    if (ImGui::BeginCombo("##material_selection", selectedMaterial))
    {
        for (int n = 0; n < IM_ARRAYSIZE(materials); n++)
        {
            bool isSelected = (selectedMaterial == materials[n]);

            if (ImGui::Selectable(materials[n], isSelected))
            {
                selectedMaterial = materials[n];

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }

        ImGui::EndCombo();
    }

    ImGui::End();
}

// Returns true on full success.
bool InitSDL(SDL_Window*& window, SDL_Renderer*& renderer)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cout << "SDL initialization failed: " << SDL_GetError() << std::endl;
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

void ClearWindow(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

int main(int argc, char* argv[])
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    bool shouldQuit = false;

    if (!InitSDL(window, renderer) || !InitImGui(window, renderer))
    {
        std::cout << "Error on SDL or ImGui init!" << std::endl;
    }

    ImGuiIO& io = ImGui::GetIO();

    ClearWindow(renderer);

    std::vector<std::unique_ptr<Particle>> cells;

    int width = std::floor(WINDOW_WIDTH / CELL_SIZE);
    int height = std::floor(WINDOW_HEIGHT / CELL_SIZE);

    for (int i = 0; i < width * height; i++)
    {
        std::unique_ptr<Particle> newCell = std::make_unique<Particle>();

        newCell->material.name = MATERIAL_NAME_NONE;
        newCell->material.type = MATERIAL_TYPE_NONE;
        cells.push_back(std::move(newCell));
    }

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

        OnImGuiRenderBrushDropDown();
        OnImGuiRenderMaterialDropdown();

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
