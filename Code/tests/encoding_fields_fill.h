#pragma once

// Fills a message's fields with distinct values, the same every run: the recorded bytes depend on it, so changing a
// value here means recording again from serializers known to be right.
#include <Wire/Archive.h>

#include <string>
#include <type_traits>

struct FillArchive
{
    static constexpr bool kReading = true;

    template <class... T> void operator()(T&&... aFields) noexcept { (Field(aFields), ...); }

    // Whether the outermost value had fields or a fill of its own.
    [[nodiscard]] bool Filled() const noexcept { return m_filled; }

    // A distinct integer, never 0, wider than a byte.
    uint64_t Next() noexcept { return (m_counter++ * 0x1011) | 1; }

    template <class T> void Field(T& aValue) noexcept
    {
        const bool cOuter = m_depth++ == 0;
        if constexpr (std::is_same_v<T, bool>)
            aValue = (m_counter++ % 2) == 1;
        else if constexpr (std::is_enum_v<T>)
            aValue = static_cast<T>(m_enumValue);
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
            aValue = static_cast<T>(-static_cast<int64_t>(Next() & 0x7FFFFFFF));
        else if constexpr (std::is_integral_v<T>)
            aValue = static_cast<T>(Next());
        else if constexpr (std::is_floating_point_v<T>)
            aValue = static_cast<T>(m_counter++) + T(0.25);
        else if constexpr (std::is_same_v<T, std::string>)
            aValue = "s" + std::to_string(m_counter++);
        else if constexpr (Wire::kIsArray<T>)
            for (auto& element : aValue)
                Field(element);
        else if constexpr (Wire::kIsOptional<T>)
            Field(aValue.emplace());
        else if constexpr (Wire::kHasFields<T>)
        {
            aValue.Fields(*this);
            m_filled |= cOuter;
        }
        else if constexpr (requires { FillValue(*this, aValue); })
        {
            FillValue(*this, aValue);
            m_filled |= cOuter;
        }
        --m_depth;
    }
    template <size_t N, class T> void Field(Wire::BitsField<N, T>& aField) noexcept
    {
        if constexpr (std::is_enum_v<T> || std::is_same_v<T, bool>)
            Field(aField.Value);
        else
            aField.Value = static_cast<T>(N >= 64 ? Next() : Next() & ((uint64_t{1} << N) - 1));
    }
    template <class TAs, class T> void Field(Wire::AsField<TAs, T>& aField) noexcept { Field(aField.Value); }
    template <class T, class TEach> void Field(Wire::ListField<T, TEach>& aField) noexcept
    {
        const uint64_t cCount = aField.Max < 2 ? aField.Max : 2;
        const uint64_t cEnum = m_enumValue;
        aField.Value.clear();
        for (uint64_t i = 0; i < cCount; ++i)
        {
            // Each element its own enum value: a conditional field shows in one.
            m_enumValue = i + 1;
            if constexpr (Wire::kIsMap<T>)
            {
                typename T::key_type key{};
                typename T::mapped_type value{};
                (*this)(key, value);
                aField.Value.insert_or_assign(key, std::move(value));
            }
            else
                aField.Each(*this, aField.Value.emplace_back());
        }
        m_enumValue = cEnum;
    }

private:
    uint64_t m_counter = 1;
    uint64_t m_enumValue = 1;
    int m_depth = 0;
    bool m_filled = false;
};

// The types that write themselves: their fills, values their quantizers keep.
inline void FillValue(FillArchive& aFill, Vector3_NetQuantize& aValue)
{
    aFill(aValue.x, aValue.y, aValue.z);
}
inline void FillValue(FillArchive& aFill, Rotator2_NetQuantize& aValue)
{
    aFill(aValue.x, aValue.y);
}
inline void FillValue(FillArchive& aFill, Mods& aValue)
{
    for (int i = 0; i < 2; ++i)
    {
        auto& entry = aValue.ModList.emplace_back();
        aFill(entry.Filename, entry.Id, entry.IsLite);
    }
}
inline void FillValue(FillArchive& aFill, Tints& aValue)
{
    for (int i = 0; i < 2; ++i)
    {
        auto& entry = aValue.Entries.emplace_back();
        aFill(entry.Name, entry.Alpha, entry.Color, entry.Type);
    }
}
inline void FillValue(FillArchive& aFill, Inventory::Entry& aValue)
{
    aFill(aValue.BaseId, aValue.Count, aValue.ExtraCharge, aValue.ExtraEnchantId, aValue.ExtraEnchantCharge, aValue.EnchantData.IsWeapon,
          Wire::List(aValue.EnchantData.Effects, 2), aValue.ExtraHealth, aValue.ExtraPoisonId, aValue.ExtraPoisonCount, aValue.ExtraSoulLevel,
          aValue.ExtraName, aValue.ExtraOwner, aValue.ExtraEnchantRemoveUnequip, aValue.ExtraWorn, aValue.ExtraWornLeft, aValue.IsQuestItem);
}
inline void FillValue(FillArchive& aFill, Inventory& aValue)
{
    aFill(Wire::List(aValue.Entries, 2), aValue.CurrentMagicEquipment.LeftHandSpell, aValue.CurrentMagicEquipment.RightHandSpell,
          aValue.CurrentMagicEquipment.Shout);
}
inline void FillValue(FillArchive& aFill, RecordPart& aValue)
{
    aValue.Kind = RecordKind::kAiState;
    aFill(aValue.Version, aValue.Bytes);
}
inline void FillValue(FillArchive& aFill, ActionEvent& aValue)
{
    aFill(aValue.Tick, aValue.ActionId, aValue.TargetId, aValue.TargetActor, aValue.IdleId, aValue.State1, aValue.State2, aValue.StateAfter1,
          aValue.StateAfter2, aValue.Type, aValue.EventName, aValue.TargetEventName, aValue.GraphState);
}
inline void FillValue(FillArchive& aFill, Movement& aValue)
{
    aFill(aValue.CellId, aValue.WorldSpaceId, aValue.Position, aValue.Rotation, aValue.Teleported, aValue.Lying, aValue.Knocked, aValue.OnGround,
          aValue.CapsuleCollides, aValue.GraphsStepPhysics);
    aValue.Direction = 0.5f;
    aValue.BodiesLayer = 33;
}
inline void FillValue(FillArchive& aFill, ReferenceUpdate& aValue)
{
    aFill(aValue.UpdatedMovement, Wire::List(aValue.ActionEvents, 2));
}
inline void FillValue(FillArchive& aFill, BodyState& aValue)
{
    aFill(aValue.Frame.Key.Object, aValue.Frame.Key.Actor, aValue.Frame.Key.Index, aValue.Frame.Position.x, aValue.Frame.Position.y,
          aValue.Frame.Position.z, aValue.Frame.LinearVelocity.x, aValue.Frame.Gravity, aValue.Author, aValue.AuthSeq, aValue.HoldSeq, aValue.Held);
    aValue = aValue.Quantized();
}
inline void FillValue(FillArchive& aFill, GraphOps& aValue)
{
    aFill(aValue.Frames.emplace_back().Tick);
    aValue.Frames.back().Step = 0.25f;
}
inline void FillValue(FillArchive& aFill, ScriptCall& aValue)
{
    aFill(aValue.Class, aValue.Function);
    aValue.Self = ScriptForm{3, GameId{1, 2}};
    aValue.Arguments = {ScriptValue{int32_t{-4}}, ScriptValue{1.5f}, ScriptValue{true}, ScriptValue{std::string("x")}};
}
inline void FillValue(FillArchive& aFill, PlayerOwnState& aValue)
{
    aValue.Present = true;
    aFill(Wire::List(aValue.FavoriteItems, 2, [](auto& aItemFill, PlayerOwnState::FavoriteItem& aItem) {
              aItemFill(aItem.Stack);
              aItem.Hotkey = 3;
          }),
          aValue.Damage, aValue.Trainings, aValue.FirstPerson);
    aValue.Effects.push_back({GameId{1, 5}, 2, 1.5f, 10.f});
    aValue.Cooldowns.push_back({GameId{1, 6}, 30.f});
}
inline void FillValue(FillArchive& aFill, PlayerRecord& aValue)
{
    aFill(aValue.Key, aValue.Name, aValue.Level, aValue.Base, aValue.BaseFlags, aValue.FaceTints, aValue.Weight, aValue.LevelExperience,
          aValue.LevelThreshold, aValue.PerkPoints, Wire::List(aValue.Perks, 2), Wire::List(aValue.Spells, 2), aValue.Items, aValue.Cell,
          aValue.WorldSpace, aValue.Position, Wire::List(aValue.Words, 2), Wire::List(aValue.Recipes, 2), aValue.Own);
    for (auto& skill : aValue.Skills)
        aFill(skill.Level, skill.Experience, skill.Threshold, skill.Legendary);
    aValue.Values = {{12, 100.f}, {24, 50.f}};
    aValue.PermanentValues = {{26, 110.f}};
    aValue.Extras["racemenu"] = "blob";
    aValue.Knowledge.Present = true;
    aValue.Knowledge.Ingredients = {{GameId{0, 0x3AD56}, 0x5, 2}};
    aValue.Knowledge.Enchantments = {GameId{0, 0x4605A}};
}

// The messages that write themselves.
inline void FillValue(FillArchive& aFill, AuthenticationRequest& aValue)
{
    aFill(aValue.Game, aValue.Version, aValue.UserMods, aValue.Username, aValue.WorldSpaceId, aValue.CellId, aValue.Level, aValue.PlayerTime,
          aValue.PlayerKey, aValue.WantsWorld, aValue.HasCharacter);
    aValue.HeldSaves = {std::string(32, 'a'), std::string(32, 'b')};
}
inline void FillValue(FillArchive& aFill, AnnounceCharacterRequest& aValue)
{
    aFill(aValue.Tick, aValue.SessionId, aValue.ReferenceId, aValue.FormId, aValue.CellId, aValue.WorldSpaceId, aValue.Position, aValue.Rotation,
          aValue.ChangeFlags, aValue.AppearanceBuffer, aValue.LatestAction, aValue.FaceTints, aValue.Weight, aValue.IsDragon, aValue.IsMount,
          aValue.CommanderId, aValue.CurrentActorData, aValue.GraphState, aValue.Record, aValue.PerksContent, Wire::List(aValue.EffectIds, 2),
          Wire::List(aValue.HeldEffects, 2));
}
inline void FillValue(FillArchive& aFill, NotifyCharacterSpawn& aValue)
{
    aFill(aValue.Tick, aValue.SessionId, aValue.FormId, aValue.BaseId, aValue.CellId, aValue.Position, aValue.Rotation, aValue.ChangeFlags,
          aValue.AppearanceBuffer, aValue.LatestAction, aValue.FaceTints, aValue.Weight, aValue.InitialActorValues, aValue.PlayerId, aValue.IsDead,
          aValue.IsPlayer, aValue.CommanderId, aValue.GraphState, aValue.Record, aValue.PerksContent, Wire::List(aValue.EffectIds, 2),
          Wire::List(aValue.HeldEffects, 2));
}
inline void FillValue(FillArchive& aFill, EnterCellRequest& aValue)
{
    aFill(aValue.Tick, aValue.CellId, aValue.WorldSpaceId, aValue.Position, aValue.Heading, aValue.How, aValue.LatestAction, aValue.GraphState,
          aValue.Data);
}
inline void FillValue(FillArchive& aFill, RequestRespawn& aValue)
{
    aFill(aValue.ActorId, aValue.AppearanceBuffer, aValue.ChangeFlags, aValue.FaceTints, aValue.Weight, aValue.LatestAction, aValue.GraphState,
          aValue.Data);
    aValue.HasState = true;
}
inline void FillValue(FillArchive& aFill, CrimeRequest& aValue)
{
    aFill(aValue.Sequence, aValue.Target, aValue.TargetActor, aValue.Item, aValue.Owner, aValue.Count, aValue.GoToJail, aValue.RemoveStolenItems,
          aValue.AllowWarning);
    aValue.Action = CrimeAction::ContainerTheft;
    for (int i = 0; i < 2; ++i)
    {
        auto& item = aValue.Items.emplace_back();
        aFill(item.Item, item.Taking, item.Owners.emplace_back().Owner);
        item.Owners.back().Count = 7;
    }
}
inline void FillValue(FillArchive& aFill, NotifyStealthEye& aValue)
{
    aFill(aValue.Seeing, aValue.Stealth);
    aValue.Level = 2;
}
inline void FillValue(FillArchive& aFill, Payloads::RoutedCall& aValue)
{
    aFill(aValue.SessionId, aValue.Source, aValue.ReplyId, aValue.Call);
    aValue.To = CallTo::kOwner;
    aValue.Controls = CallControls::kTakes;
}
inline void FillValue(FillArchive& aFill, Payloads::CallEnded& aValue)
{
    aFill(aValue.Source, aValue.Key);
    aValue.Result = ScriptForm{4, GameId{0, 0x14}};
}
inline void FillValue(FillArchive& aFill, Payloads::PlayerNativeResult& aValue)
{
    aFill(aValue.ReplyId, aValue.SessionId);
    aValue.Result = std::string("result");
}
inline void FillValue(FillArchive& aFill, Payloads::GraphOpsBatch& aValue)
{
    aFill(aValue.Ops);
}
