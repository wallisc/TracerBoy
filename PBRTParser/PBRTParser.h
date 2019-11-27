#pragma once
#include "SceneParser.h"
#include <sal.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stack>


#define PBRTPARSER_STRINGBUFFERSIZE 200

namespace PBRTParser
{
// Keep this inside our namespace because glm doesn't protect
// against double inclusion
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#include "glm/glm.hpp"

struct AreaLightAttribute
{
    SceneParser::Vector3 m_lightColor;
};

class Attributes
{
public:
    typedef enum _ObjectType
    {
        Normal,
        AreaLight
    } ObjectType;

    Attributes()
    {
        m_Type = Normal;
    }

    Attributes(const Attributes &attributes)
    {
        memcpy(this, &attributes, sizeof(*this));
    }

    Attributes operator=(const Attributes &attributes)
    {
        return Attributes(attributes);
    }

    Attributes(AreaLightAttribute &attribute)
    {
        m_Type = AreaLight;
        m_areaLightAttribute = attribute;
    }

    ObjectType GetType() { return m_Type; }
    AreaLightAttribute &GetAreaLightAttribute() 
    { 
        assert(GetType() == AreaLight);
        return m_areaLightAttribute;
    }

private:
    ObjectType m_Type;
    union
    {
        AreaLightAttribute m_areaLightAttribute;
    };
};

class PBRTParser : public SceneParser::SceneParserClass
{
    public:
        PBRTParser();
        ~PBRTParser();
        virtual void Parse(std::string filename, SceneParser::Scene &outputScene);

    private:
        void ParseFilm(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseCamera(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseWorld(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseMaterial(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseMesh(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseTexture(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseLightSource(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseAreaLightSource(std::ifstream &fileStream, SceneParser::Scene &outputScene);
        void ParseTransform();

        void ParseShape(std::ifstream &fileStream, SceneParser::Scene &outputScene, SceneParser::Mesh &mesh);

        void ParseBracketedVector3(std::istream, float &x, float &y, float &z);

        void InitializeDefaults(SceneParser::Scene &outputScene);
        void InitializeCameraDefaults(SceneParser::Camera &camera);

        static std::string CorrectNameString(const char *pString);
        static std::string CorrectNameString(const std::string &str);

        void GetTempCharBuffer(char **ppBuffer, size_t &charBufferSize)
        {
            *ppBuffer = _m_buffer;
            charBufferSize = ARRAYSIZE(_m_buffer);
        };

        char *GetLine()
        {
            char *pTempBuffer;
            size_t bufferSize;
            GetTempCharBuffer(&pTempBuffer, bufferSize);

            m_fileStream.getline(pTempBuffer, bufferSize);

            lastParsedWord = "";
            return pTempBuffer;
        }

        std::stringstream GetLineStream()
        {
            char *pTempBuffer;
            size_t bufferSize;
            GetTempCharBuffer(&pTempBuffer, bufferSize);

            m_fileStream.getline(pTempBuffer, bufferSize);

            return std::stringstream(std::string(pTempBuffer));
        }

        static SceneParser::Vector3 ConvertToVector3(const glm::vec3 &vec)
        {
            return SceneParser::Vector3(vec.x, vec.y, vec.z);
        }

        static SceneParser::Vector3 ConvertToVector3(const glm::vec4 &vec)
        {
            return SceneParser::Vector3(vec.x, vec.y, vec.z);
        }

        Attributes &GetCurrentAttributes()
        {
            return m_AttributeStack.top();
        }
        
        void SetCurrentAttributes(const Attributes &attritbutes)
        {
            m_AttributeStack.top() = attritbutes;
        }

        float ParseFloat1(std::istream &inStream);
        std::string ParseString(std::istream &inStream);
        void ParseExpectedWords(std::istream &inStream, _In_reads_(numWords) std::string *pWords, UINT numWords);
        void ParseExpectedWord(std::istream &inStream, const std::string &word);

        std::string GenerateCheckerboardTexture(std::string fileName, float uScale, float vScale, SceneParser::Vector3 color1, SceneParser::Vector3 color2);
        void GenerateBMPFile(std::string fileName, _In_reads_(width * height)SceneParser::Vector3 *pDmageData, UINT width, UINT height);

        std::ifstream m_fileStream;
        std::string m_CurrentMaterial;
        std::stack<Attributes> m_AttributeStack;
        std::unordered_map<std::string, std::string> m_TextureNameToFileName;

        glm::mat4 m_currentTransform;
        glm::vec4 m_lookAt;
        glm::vec4 m_camPos;
        glm::vec4 m_camUp;

        // Shouldn't be accessed directly outside of GetTempCharBuffer
        char _m_buffer[500];
        std::string lastParsedWord;
        std::string m_relativeDirectory;
    };
}
