/****************************************************************************\
 * Pilot Alex, 2022-2024, All right reserved. Copyright (c) 2023.           *
 * Made by A.G. under the username of Pilot Alex.                           *
 * C++17, Visual Studio 2022.                                               *
\****************************************************************************/

#include <map>
#include <cmath>
#include <string>
#include <random>
#include <vector>
#include <iostream>

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui with std::string
#include "imgui_sdl_backend/imgui_impl_sdl2.h"
#include "imgui_sdl_backend/imgui_impl_sdlrenderer2.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#undef main

constexpr int CELL_SIZE = 10;
constexpr int WINDOW_HEIGHT = 800;
constexpr int WINDOW_WIDTH = 800;

// --------------------------------------------------------------------------------------------

enum class MaterialType
{
    None,   // Used to represent an empty cell/particle
    Solid,  // Solid materials such as sand, salt and more
    Liquid, // Liquid materials such as water, lava and more
    Gas     // Gas materials such as toxic gas and more
};

enum class BrushType
{
    Small  = 1, // Reveal a single particle at once
    Medium = 8, // Reveal particles in located in a rect with an extent of 8
    Big    = 16 // Reveal particles in located in a rect with an extent of 16
};

static BrushType selectedBrush = BrushType::Small;
static MaterialType selectedMaterial = MaterialType::Solid;

// --------------------------------------------------------------------------------------------

struct SpreadRules
{
    int spreadSpeed;
    std::vector<MaterialType> canReplace;
    std::map<MaterialType, SDL_Color> contactColors;
    std::map<MaterialType, std::string> contactSounds;
};

struct Particle
{
    float lifeTime;
    bool hasBeenUpdatedThisFrame;
    SpreadRules spreadRules;
    MaterialType materialType;

    Particle() = delete;
    Particle(const SpreadRules& spreadRules, MaterialType materialType)
        : lifeTime(-1.0f)
        , hasBeenUpdatedThisFrame(false)
        , spreadRules(spreadRules)
        , materialType(materialType)
    {
    }
};

using Grid = std::vector<Particle>;

// --------------------------------------------------------------------------------------------

static std::default_random_engine rng;
static std::random_device rd;
static std::mt19937 gen(rd());

// Function to generate a random float between min and max (inclusive)
float RandomFloat(float min, float max)
{
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

// --------------------------------------------------------------------------------------------

// Returns a new rgb color as an SDL_Color struct that the particle p should
// take when in collides with the material with type.
SDL_Color GetParticleColorOnCollision(const Particle& p, const Particle& target)
{
    const auto& contactColors = p.spreadRules.contactColors;

    auto it = contactColors.find(target.materialType);
    if (it != contactColors.end())
    {
        return it->second;
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

// Returns true if the particle p is allowed to spread and replace the material type.
bool ParticleCanSpreadTo(const Particle& p, const Particle& target)
{
    const auto& spreadRules = p.spreadRules;
    const auto& canReplace = spreadRules.canReplace;

    auto it = std::find(canReplace.begin(), canReplace.end(), target.materialType);

    if (it != canReplace.end())
    {
        return true;
    }

    return false;
}

// Returns wether the cell located at x and y on the grid is empty or not.
bool CellIsEmpty(const Grid& cells, int gridWidth, int x, int y)
{
    int index = GetCellIndex(x, y, gridWidth);
    return cells[index].materialType == MaterialType::None;
}

// Returns wether the cell located at x and y on the grid is empty or not.
bool ParticleIsEmpty(const Particle& p)
{
    return p.materialType == MaterialType::None;
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
Particle* GetParticleAt(Grid& cells, int gridWidth, int x, int y)
{
    int index = GetCellIndex(gridWidth, x, y);
    if (index >= 0 && index < static_cast<int>(cells.size()))
    {
        return &cells[index];
    }
    return nullptr;
}

// --------------------------------------------------------------------------------------------

// Transforms a mouse coordinates tuple to a row and column accordingly to the grid.
SDL_Point MouseCoordinatesToXY(int gridWidth, int gridHeight, int cellSize, int mouseX, int mouseY)
{
    int x = mouseX / cellSize;
    int y = mouseY / cellSize;

    // Clamp the coordinates within the valid range
    x = std::max(0, std::min(x, gridWidth - 1));
    y = std::max(0, std::min(y, gridHeight - 1));

    return SDL_Point{ x, y };
}

// --------------------------------------------------------------------------------------------

// Lights up a particle from the grid located at x and y.
void RevealParticleAt(Grid& cells, int gridWidth, int x, int y)
{
    Particle* spawnParticle = GetParticleAt(cells, gridWidth, x, y);
    if (spawnParticle)
    {
        spawnParticle->materialType = selectedMaterial;
    }
}

// Lights up particles from the grid located in the bounds.
void RevealParticlesAt(Grid& cells, int gridWidth, const SDL_Rect& bounds)
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

        RevealParticleAt(cells, gridWidth, x, y);
    }
}

// p1 becomes p2 and p2 becomes p1.
void SwapParticles(Particle& p1, Particle& p2)
{
    std::swap(p1, p2);
}

// Updates the solid particle located at x and y on the grid.
void UpdateSolid(Grid& cells, int gridWidth, int x, int y)
{
    Particle* solidParticle = GetParticleAt(cells, gridWidth, x, y);

    // Get neighboring particles
    Particle* bParticle = GetParticleAt(cells, gridWidth, x, y + 1); // Below
    Particle* blParticle = GetParticleAt(cells, gridWidth, x - 1, y + 1); // Below left
    Particle* brParticle = GetParticleAt(cells, gridWidth, x + 1, y + 1); // Below right

    if (bParticle && (ParticleIsEmpty(*bParticle) || ParticleCanSpreadTo(*solidParticle, *bParticle))) // Move down
    {
        SwapParticles(*bParticle, *solidParticle);
    }

    else if (blParticle && ParticleIsEmpty(*blParticle)) // Move down and left
    {
        SwapParticles(*blParticle, *solidParticle);
    }

    else if (brParticle && ParticleIsEmpty(*brParticle)) // Move down and right
    {
        SwapParticles(*brParticle, *solidParticle);
    }
}

// Updates the liquid particle located at x and y on the grid.
void UpdateLiquid(Grid& cells, int gridWidth, int x, int y)
{
    Particle* liquidParticle = GetParticleAt(cells, gridWidth, x, y);

    // Get neighboring particles
    Particle* lParticle = GetParticleAt(cells, gridWidth, x - 1, y); // Left
    Particle* rParticle = GetParticleAt(cells, gridWidth, x + 1, y); // Right

    Particle* bParticle = GetParticleAt(cells, gridWidth, x, y + 1); // Below
    Particle* blParticle = GetParticleAt(cells, gridWidth, x - 1, y + 1); // Below left
    Particle* brParticle = GetParticleAt(cells, gridWidth, x + 1, y + 1); // Below right

    if (bParticle && (ParticleIsEmpty(*bParticle) || ParticleCanSpreadTo(*liquidParticle, *bParticle))) // Move down
    {
        SwapParticles(*bParticle, *liquidParticle);
    }

    else if (blParticle && ParticleIsEmpty(*blParticle)) // Move down and left
    {
        SwapParticles(*blParticle, *liquidParticle);
    }

    else if (brParticle && ParticleIsEmpty(*brParticle)) // Move down and right
    {
        SwapParticles(*brParticle, *liquidParticle);
    }

    else if (lParticle && ParticleIsEmpty(*lParticle)) // Move left
    {
        SwapParticles(*lParticle, *liquidParticle);
    }

    else if (rParticle && ParticleIsEmpty(*rParticle)) // Move right
    {
        SwapParticles(*rParticle, *liquidParticle);
    }
}

// Updates the gas particle located at x and y on the grid.
void UpdateGas(Grid& cells, int gridWidth, int x, int y)
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
        if (direction && (ParticleIsEmpty(*direction) || ParticleCanSpreadTo(*gasParticle, *direction)))
        {
            SwapParticles(*direction, *gasParticle);
            break;
        }
    }
}

// Updates the inputs related the the material selection.
void UpdateInputs(const SDL_Event& event, const ImGuiIO& io, Grid& cells, int gridWidth, int gridHeight)
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

        switch (selectedBrush)
        {
        case BrushType::Small:
        {
            const SDL_Point coords = MouseCoordinatesToXY(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY);
            RevealParticleAt(cells, gridWidth, coords.x, coords.y);
            break;
        }
        case BrushType::Medium:
        case BrushType::Big:
        {
            int brushSize = static_cast<std::underlying_type<BrushType>::type>(selectedBrush);
            const SDL_Rect bounds = MouseCoordinatesToBounds(gridWidth, gridHeight, CELL_SIZE, mouseX, mouseY, brushSize);
            RevealParticlesAt(cells, gridWidth, bounds);
            break;
        }
        default:
            break;
        }
    }
}

// Updates the particles motion.
void UpdateParticleSimulation(SDL_Renderer* renderer, Grid& cells, int gridWidth, int gridHeight)
{
    for (int y = gridHeight - 1; y > 0; y--)
    {
        for (int x = 0; x < gridWidth; x++)
        {
            MaterialType matType = GetParticleAt(cells, gridWidth, x, y)->materialType;

            switch (matType)
            {
            case MaterialType::None:
                break;

            case MaterialType::Solid:
                UpdateSolid(cells, gridWidth, x, y);
                break;

            case MaterialType::Liquid:
                UpdateLiquid(cells, gridWidth, x, y);
                break;

            case MaterialType::Gas:
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
            SDL_Color color = GetParticleColorOnCollision(*currentParticle, *currentParticle);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

// Renders the UI related to the brush type selection.
void RenderBrushSelectionDropdown()
{
    static std::vector<std::string> brushOptions = { "Small (1px)", "Medium (extent = 8)", "Big (extent = 16)" };
    static int selectedBrushOption = 0;

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
                    selectedBrush = BrushType::Small;
                    break;

                case 1:
                    selectedBrush = BrushType::Medium;
                    break;

                case 2:
                    selectedBrush = BrushType::Big;
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
    static std::vector<std::string> materialOptions = { "Solid", "Liquid", "Gas" };
    static int selectedMaterialOption = 0;

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

// Renders the entire UI in one same call.
void RenderImGui()
{
    ImGui::NewFrame();

    if (ImGui::Begin("Panel"))
    {
        if (ImGui::BeginTabBar("tab_bar"))
        {
            if (ImGui::BeginTabItem("Controls"))
            {
                RenderBrushSelectionDropdown();
                RenderMaterialSelectionDropdown();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();

        }

        ImGui::End();
    }

    ImGui::Render();
}

// Destroy ImGui and SDL related elements.
void Shutdown(SDL_Window* window, SDL_Renderer* renderer)
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
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

    const int gridWidth = WINDOW_WIDTH / CELL_SIZE;
    const int gridHeight = WINDOW_HEIGHT / CELL_SIZE;

    Grid cells;
    for (int i = 0; i < gridWidth * gridHeight; i++)
    {
        SpreadRules rules;
        rules.canReplace = { MaterialType::None };
        rules.contactColors =
        {
            {
                MaterialType::None, SDL_Color{255,255,255,255}
            } 
        };
        rules.spreadSpeed = 1;

        cells.push_back(Particle(rules, MaterialType::None));
    }

    const ImGuiIO& io = ImGui::GetIO();

    // Game loop
    while (!shouldQuit)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            UpdateInputs(event, io, cells, gridWidth, gridHeight);

            if (event.type == SDL_QUIT)
            {
                shouldQuit = true;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        RenderImGui();

        SDL_RenderClear(renderer);
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

        UpdateParticleSimulation(renderer, cells, gridWidth, gridHeight);

        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    Shutdown(window, renderer);

    return 0;
}
