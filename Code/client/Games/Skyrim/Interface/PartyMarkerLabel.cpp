#include <Interface/PartyMarkerLabel.h>
#include <Interface/FxDelegateArgs.h>
#include <Interface/GFxMovie.h>

void SetPartyMarkerLabel(GFxMovie& aMovie, const std::string& acMarker, const std::string& acName)
{
    GFxValue text{};
    text.type = GFxValue::kString;
    text.value.string = acName.c_str();
    GFxValue yes{};
    yes.type = GFxValue::kBoolean;
    yes.value.boolean = true;
    aMovie.SetVariable((acMarker + ".label").c_str(), text, 0);
    aMovie.SetVariable((acMarker + ".textField.noTranslate").c_str(), yes, 0);
    aMovie.SetVariable((acMarker + ".textField.text").c_str(), text, 0);
    aMovie.SetVariable((acMarker + ".textField._visible").c_str(), yes, 0);
    // Vanilla's unhovered marker has an opaque text field inside a transparent TextClip.
    GFxValue opacity{};
    opacity.type = GFxValue::kNumber;
    opacity.value.number = 100;
    aMovie.SetVariable((acMarker + ".TextClip._visible").c_str(), yes, 0);
    aMovie.SetVariable((acMarker + ".TextClip._alpha").c_str(), opacity, 0);
}
