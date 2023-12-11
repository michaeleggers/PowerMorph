#include "editor.h"

#include <stdlib.h>

#include <vector>
#include <algorithm>

#include <glad/glad.h>

#include "imgui.h"
#include "tinyfiledialogs.h"

#include "batch.h"
#include "fbo.h"
#include "shader.h" 
#include "image.h"
#include "input.h"
#include "static_geometry.h"
#include "render_common.h"
#include "common.h"
#include "beierneely.h"
#include "parser.h"

std::string LineToString(Line& line) {
    std::string result;
    // Image coordinates that the beier-neely algorithm uses
    glm::vec3 aPos = line.a.pos;
    glm::vec3 bPos = line.b.pos;
    // Info for the editor to display lines correctly
    ImVec2 absA = line.absA;
    ImVec2 absB = line.absB;
    ImVec2 editorScale = line.buttonSize; // TODO: Rename buttonSize -> widgetSize or something like that

    result += std::to_string(aPos.x); result += " ";
    result += std::to_string(aPos.y); result += " ";
    result += std::to_string(absA.x); result += " ";
    result += std::to_string(absA.y); result += " ";
    result += std::to_string(editorScale.x); result += " ";
    result += std::to_string(editorScale.y);

    return result;
}

void WriteProjectFile(std::string pathAndFileName, std::vector<Line>& sourceLines, std::vector<Line>& destLines, std::string sourceImagePath, std::string destImagePath) {
    // Create string to write
    const char* dataToSave = "This is the data to be saved in the file.";
    std::string result;
    result += "src_img_path " + sourceImagePath + "\n";
    result += "dst_img_path " + destImagePath+ "\n";

    size_t numLinesPairs = sourceLines.size();
    // TODO: Give warning if sourceLines.size() != destLines.size()
    result += "src\n";
    for (size_t i = 0; i < numLinesPairs; i++) {
        std::string lineString = std::to_string(i) + " " + LineToString(sourceLines[i]);
        result += lineString + "\n";
    }
    result += "dst\n";
    for (size_t i = 0; i < numLinesPairs; i++) {
        std::string lineString = std::to_string(i) + " " + LineToString(destLines[i]);
        result += lineString + "\n";
    }

    FILE* file = fopen(pathAndFileName.c_str(), "w"); // Open the file in write mode
    if (file != NULL) {
        fwrite(result.data(), sizeof(char), result.size(), file);
        fclose(file);
        SDL_Log("Data saved to %s\n", pathAndFileName.c_str());
    }
    else {
        SDL_Log("Error opening file for writing.\n");
    }
}

void Editor::InitFromProjectFile(std::string pathAndFilename) {    
    ATP_File projectFile;
    if (atp_read_file(pathAndFilename.c_str(), &projectFile) != ATP_SUCCESS) {
        SDL_Log("Failed to read project file: %s.\n", pathAndFilename.c_str());
        return;
    }
    MorphProjectData projectData = ParseProjectFile(projectFile);
}

static ImVec2 MousePosToImageCoords(ImVec2 mousePos, ImVec2 widgetMins, ImVec2 widgetSize, ImVec2 imageSize) {
    ImVec2 mousePosInButton = ImVec2(mousePos.x - widgetMins.x, mousePos.y - widgetMins.y);
    ImVec2 pictureCoords = ImVec2(
        imageSize.x * (mousePosInButton.x / widgetSize.x),
        imageSize.y * (mousePosInButton.y / widgetSize.y)
    );
    
    return pictureCoords;
}


// NOTE: Keep this as a reference if we want to render own geometry on top of the imgui image FBO
// 
//void ShowWindow(const char* title, Framebuffer& fbo, 
//    Shader& shader, Image& image, Batch& batch, std::vector<Line>& lines, EditorWindowType windowType)
//{
    // 4.) Bind batch and render (will be the lines later)

    //fbo.Bind();
    //glViewport(0, 0, fbo.m_Width, fbo.m_Height);
    ////glm::mat4 ortho = glm::ortho(0.0f, 500.0f, 0.0f, 500.0f, 0.1f, 100.0f);
    ////glm::mat4 ortho = glm::perspective(glm::radians(90.0f), imguiWindowWidth / imguiWindowHeight, 0.1f, 100.0f);
    //glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    //glClear(GL_COLOR_BUFFER_BIT);
    //shader.Activate();
    //glBindTexture(GL_TEXTURE_2D, fbo.GetTexture().GetHandle());
    //batch.Bind();
    ////GLint orthoMatrixLocation = glGetUniformLocation(imageShader.Program(), "u_Ortho");
    ////glUniformMatrix4fv(orthoMatrixLocation, 1, GL_FALSE, glm::value_ptr(ortho));
    //glDrawElements(GL_TRIANGLES, batch.IndexCount(), GL_UNSIGNED_INT, nullptr);
    //fbo.Unbind();
//}

Editor::Editor(Image sourceImage, Image destImage)
{
    // TODO: Should we allow source and destination images to be of different size?
    //       If yes, what is the size of the result image?
    assert(sourceImage.m_Width == destImage.m_Width);
    assert(sourceImage.m_Height == destImage.m_Height);

    m_editorState = ED_IDLE;
    m_editorMouseState = ED_MOUSE_IDLE;
    m_editorMouseInfo = {ImVec2(0, 0), ImVec2(0, 0)};

    m_sourceImage = sourceImage;
    m_destImage = destImage;

    m_A = 0.001f;
    m_B = 2.5f;
    m_P = 0.0f;

    m_NumIterations = 2;
    m_MaxIterations = 100;
    m_ImageIndex = 0;

    // Create Framebuffers for windows
    m_sourceFBO = new Framebuffer(sourceImage.m_Width, sourceImage.m_Height);
    m_destFBO = new Framebuffer(destImage.m_Width, destImage.m_Height);
    m_resultFBO = new Framebuffer(sourceImage.m_Width, sourceImage.m_Height);

    // Shader
    std::string exePath = com_GetExePath();
#ifdef WIN32
    if (!m_imageShader.Load(exePath + "../../shaders/basic.vert", exePath + "../../shaders/basic.frag")) {
        SDL_Log("Could not load shaders!\n");
        exit(-1);
    }   
#elif __APPLE__
    if (!m_imageShader.Load(exePath + "/../shaders/basic.vert", exePath + "/../shaders/basic.frag")) {
        SDL_Log("Could not load shaders!\n");
        exit(-1);
    }
#endif
}

Editor::~Editor()
{
    delete m_sourceFBO;
    delete m_destFBO;
    delete m_resultFBO;
}

void Editor::ShowWindow(const char* title, Image& image, Framebuffer* fbo, std::vector<Line>& lines, EditorWindowType windowType)
{
    // Setup Window to put the framebuffer into

    ImGui::Begin(title);    
    //ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    // Do not drag the window when left clicking and dragging

    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = false;
    }
    else {
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    }

    float imguiWindowWidth = ImGui::GetContentRegionAvail().x;
    float imguiWindowHeight = ImGui::GetContentRegionAvail().y;

    // safe guard for potential div by 0

    if (imguiWindowWidth <= 0) {
        imguiWindowWidth = 1.0;
    }
    if (imguiWindowHeight <= 0) {
        imguiWindowHeight = 1.0;
    }

    ImVec2 pos = ImGui::GetCursorScreenPos();    
    float srcAspect = (float)fbo->m_Width / (float)fbo->m_Height;
    float dstAspect = imguiWindowWidth / imguiWindowHeight;
    float newWidth = 0.0f;
    float newHeight = 0.0f;
    if (srcAspect > dstAspect) { // horizontal letterbox
        newWidth = imguiWindowWidth;
        newHeight = imguiWindowWidth / srcAspect;
    }
    else { // vertical letterbox
        newWidth = imguiWindowHeight * srcAspect;
        newHeight = imguiWindowHeight;
    }
    float posOffsetX = (imguiWindowWidth - newWidth) / 2.0f;
    float posOffsetY = (imguiWindowHeight - newHeight) / 2.0f;

    ImVec2 buttonSize(newWidth, newHeight); // Size of the invisible button
    ImVec2 buttonPosition(ImGui::GetCursorPosX() + posOffsetX, ImGui::GetCursorPosY() + posOffsetY); // Position of the button

    // Render the invisible button
    ImGui::SetCursorPos(buttonPosition);
    ImGui::InvisibleButton("##canvas", buttonSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);

    ImVec2 buttonMin = ImGui::GetItemRectMin();
    ImVec2 buttonMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddImage(
        (void*)fbo->GetTexture().GetHandle(),
        buttonMin,
        buttonMax,
        ImVec2(0, 0),
        ImVec2(1, 1)
    );

    ImGui::SetCursorPos(ImGui::GetWindowPos());

    // Do the editor logic here. This stuff is pretty messy and should be
    // cleaned up as soon as the program is working!

    float imageWidth = (float)image.m_Width;
    float imageHeight = (float)image.m_Height;

    if (windowType == ED_WINDOW_TYPE_SOURCE) {
        if (m_editorState == ED_IDLE) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                printf("mousePos: %f, %f\n", mousePos.x, mousePos.y);
                m_editorMouseInfo.pos1 = mousePos;
                ImVec2 pictureCoords = MousePosToImageCoords(mousePos, buttonMin, buttonSize, ImVec2(imageWidth, imageHeight));
                printf("mouse %f, %f:\n", pictureCoords.x, pictureCoords.y);
                m_editorState = ED_PLACE_SOURCE_LINE;
            }
        }
        else if (m_editorState == ED_PLACE_SOURCE_LINE) {
            ImVec2 mousePos = ImGui::GetMousePos();
            drawList->AddLine(m_editorMouseInfo.pos1, mousePos,
                ImGui::GetColorU32(ImVec4(255, 250, 0, 255)),
                5.0);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImVec2 pictureCoordsA = MousePosToImageCoords(m_editorMouseInfo.pos1, buttonMin, buttonSize, ImVec2(imageWidth, imageHeight));
                ImVec2 pictureCoordsB = MousePosToImageCoords(mousePos, buttonMin, buttonSize, ImVec2(imageWidth, imageHeight));
                ImVec2 mousePosInButtonA = ImVec2(m_editorMouseInfo.pos1.x - buttonMin.x, m_editorMouseInfo.pos1.y - buttonMin.y);
                ImVec2 mousePosInButtonB = ImVec2(mousePos.x - buttonMin.x, mousePos.y - buttonMin.y);

                lines.push_back({
                        {glm::vec3(pictureCoordsA.x, pictureCoordsA.y, 0.0f), glm::vec3(0), glm::vec2(0)},
                        {glm::vec3(pictureCoordsB.x, pictureCoordsB.y, 0.0f), glm::vec3(0), glm::vec2(0)},
                        mousePosInButtonA, mousePosInButtonB, buttonSize
                    }
                );
                printf("Lines: \n");
                for (auto& line : lines) {
                    printf("(%f, %f) -> (%f, %f)\n", line.a.pos.x, line.a.pos.y, line.b.pos.x, line.b.pos.y);
                }
                m_editorState = ED_PLACE_DEST_LINE_1;
            }
        }
    }
    else if (windowType == ED_WINDOW_TYPE_DEST) {
        if (m_editorState == ED_PLACE_DEST_LINE_1) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                m_editorMouseInfo.pos1 = mousePos;
                ImVec2 pictureCoords = MousePosToImageCoords(mousePos, buttonMin, buttonSize, ImVec2(imageWidth, imageHeight));
                m_editorState = ED_PLACE_DEST_LINE_2;
            }
        }
        else if (m_editorState == ED_PLACE_DEST_LINE_2) {
            ImVec2 mousePos = ImGui::GetMousePos();
            drawList->AddLine(m_editorMouseInfo.pos1, mousePos,
                ImGui::GetColorU32(ImVec4(255, 250, 0, 255)),
                5.0);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImVec2 pictureCoordsA = MousePosToImageCoords(m_editorMouseInfo.pos1, buttonMin, buttonSize, ImVec2(imageWidth, imageHeight));
                ImVec2 pictureCoordsB = MousePosToImageCoords(mousePos, buttonMin, buttonSize, ImVec2(imageWidth, imageHeight));
                ImVec2 mousePosInButtonA = ImVec2(m_editorMouseInfo.pos1.x - buttonMin.x, m_editorMouseInfo.pos1.y - buttonMin.y);
                ImVec2 mousePosInButtonB = ImVec2(mousePos.x - buttonMin.x, mousePos.y - buttonMin.y);

                lines.push_back({
                        {glm::vec3(pictureCoordsA.x, pictureCoordsA.y, 0.0f), glm::vec3(0), glm::vec2(0)},
                        {glm::vec3(pictureCoordsB.x, pictureCoordsB.y, 0.0f), glm::vec3(0), glm::vec2(0)},
                        mousePosInButtonA, mousePosInButtonB, buttonSize
                    }
                );
                printf("Lines: \n");
                for (auto& line : lines) {
                    printf("(%f, %f) -> (%f, %f)\n", line.a.pos.x, line.a.pos.y, line.b.pos.x, line.b.pos.y);
                }
                m_editorState = ED_IDLE;
            }
        }
    }

    pos = ImGui::GetCursorScreenPos();

    for (auto& line : lines) {
        ImVec2 absCoordsA = line.absA;
        ImVec2 absCoordsB = line.absB;
        absCoordsA.x *= buttonSize.x / line.buttonSize.x;
        absCoordsA.y *= buttonSize.y / line.buttonSize.y;
        absCoordsB.x *= buttonSize.x / line.buttonSize.x;
        absCoordsB.y *= buttonSize.y / line.buttonSize.y;
        absCoordsA.x += buttonMin.x;
        absCoordsA.y += buttonMin.y;
        absCoordsB.x += buttonMin.x;
        absCoordsB.y += buttonMin.y;
        drawList->AddLine(absCoordsA, absCoordsB,
            ImGui::GetColorU32(ImVec4(255, 255, 255, 255)),
            2.0);
    }

    //ImGui::PopStyleVar(2); // Pop both WindowPadding and ItemSpacing

    ImGui::End();

    // Draw into the framebuffer

    // TODO:
    fbo->Bind();

    glViewport(0, 0, fbo->m_Width, fbo->m_Height);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // 1.) bind a quad with the images dimension.
    Batch& unitQuadBatch = GetUnitQuadBatch();
    unitQuadBatch.Bind();
    // 2.) bind texture
    image.GetTexture().Bind();
    // 3.) render
    m_imageShader.Activate();
    glDrawElements(GL_TRIANGLES, unitQuadBatch.IndexCount(), GL_UNSIGNED_INT, nullptr);

    fbo->Unbind();
}

void Editor::ShowResultWindow(const char* title)
{
    ImGui::Begin(title);

    //ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    // Do not drag the window when left clicking and dragging

    // TODO: This is wrong! This sets global state but we don't want that. For individual input
    // capture there is something else. google it!
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = false;
    }
    else {
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    }

    float imguiWindowWidth = ImGui::GetContentRegionAvail().x;
    float imguiWindowHeight = ImGui::GetContentRegionAvail().y;

    // safe guard for potential div by 0

    if (imguiWindowWidth <= 0) {
        imguiWindowWidth = 1.0;
    }
    if (imguiWindowHeight <= 0) {
        imguiWindowHeight = 1.0;
    }

    float srcAspect = (float)m_resultFBO->m_Width / (float)m_resultFBO->m_Height;
    float dstAspect = imguiWindowWidth / imguiWindowHeight;
    float newWidth = 0.0f;
    float newHeight = 0.0f;
    if (srcAspect > dstAspect) { // horizontal letterbox
        newWidth = imguiWindowWidth;
        newHeight = imguiWindowWidth / srcAspect;
    }
    else { // vertical letterbox
        newWidth = imguiWindowHeight * srcAspect;
        newHeight = imguiWindowHeight;
    }

    float posOffsetX = (imguiWindowWidth - newWidth) / 2.0f;
    float posOffsetY = (imguiWindowHeight - newHeight) / 2.0f;

    ImVec2 imageSize(newWidth, newHeight);
    ImVec2 imagePosition(ImGui::GetCursorPosX() + posOffsetX, ImGui::GetCursorPosY() + posOffsetY);

    ImGui::SetCursorPos(imagePosition);
    ImGui::Image((void*)(intptr_t)m_blendedImages[m_ImageIndex].GetTexture().GetHandle(), ImVec2(newWidth, newHeight));
    ImGui::SetCursorPosX(imagePosition.x);
    ImGui::PushItemWidth(imageSize.x);
    ImGui::SliderInt("##imageIndexSlider", &m_ImageIndex, 0, m_blendedImages.size() - 1);

    ImGui::End();
}

void Editor::Run()
{
    ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

    ImGui::Begin("Editor");        

    ShowWindow("Source", m_sourceImage, m_sourceFBO, m_sourceLines, ED_WINDOW_TYPE_SOURCE);
    ShowWindow("Destination", m_destImage, m_destFBO, m_destLines, ED_WINDOW_TYPE_DEST);

    ImGui::Begin("Control Panel");
    const char* fileFilterList[] = { "*.mph" };
    if (ImGui::Button("Save Project")) {
        char const* retSaveFile = tinyfd_saveFileDialog(
            "Save Project",
            "",
            1,
            fileFilterList,
            "Morph MPH files");
        if (retSaveFile  == NULL) {
            SDL_Log("Save Project cancelled\n");
        }
        else {
            WriteProjectFile(retSaveFile, m_sourceLines, m_destLines, m_sourceImage.m_FilePath,  m_destImage.m_FilePath);
        }
    }
    if (ImGui::Button("Open Project")) {
        SDL_Log("Implement!\n");
        char const* retOpenFile = tinyfd_openFileDialog(
            "Open Project",
            "",
            1,
            fileFilterList,
            "Morph MPH files",
            0
        );
        if (retOpenFile == NULL) {
            SDL_Log("Open Project cancelled\n");
        }
        else {
            InitFromProjectFile(retOpenFile);
        }
    }
    ImGui::SliderFloat("a", &m_A, 0.0f, 2.0f);
    ImGui::SliderFloat("b", &m_B, 0.0f, 20.0f);
    ImGui::SliderFloat("p", &m_P, 0.0f, 1.0f);
    ImGui::SliderInt("Iterations", &m_NumIterations, 1, 100);
    if (ImGui::Button("MAGIC!")) {
        m_sourceToDestMorphs = BeierNeely(m_sourceLines, m_destLines, m_sourceImage, m_destImage, m_NumIterations, m_A, m_B, m_P);
        m_destToSourceMorphs = BeierNeely(m_destLines, m_sourceLines, m_destImage, m_sourceImage, m_NumIterations, m_A, m_B, m_P);
        std::reverse(m_destToSourceMorphs.begin(), m_destToSourceMorphs.end());
        m_blendedImages = BlendImages(m_sourceToDestMorphs, m_destToSourceMorphs);        
    }

    ImGui::End();

    if (!m_blendedImages.empty()) {
        ShowResultWindow("Result");
    }

    ImGui::End(); // Editor

}
