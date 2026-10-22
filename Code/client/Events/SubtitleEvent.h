#pragma once

struct SubtitleEvent
{
    SubtitleEvent(uint32_t aSpeakerID, std::string aText, bool aForce)
        : SpeakerID(aSpeakerID)
        , Text(aText)
        , Force(aForce)
    {
    }

    SubtitleEvent(uint32_t aSpeakerID, std::string aText, uint32_t aTopicFormID)
        : SpeakerID(aSpeakerID)
        , Text(aText)
        , TopicFormID(aTopicFormID)
    {
    }

    uint32_t SpeakerID{};
    std::string Text{};
    // This is only relevant to fallout 4
    uint32_t TopicFormID{};
    bool Force{}; // shown at any distance
};
