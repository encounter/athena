#if !defined(ATHENA_NO_SAVES) && !defined(ATHENA_NO_ZQUEST)
// This file is part of libAthena.
//
// libAthena is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// libAthena is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with libAthena.  If not, see <http://www.gnu.org/licenses/>

#include "Athena/SkywardSwordQuest.hpp"
#include "Athena/Checksums.hpp"
#include <sstream>
#include "utf8.h"
namespace Athena
{

namespace priv
{
static const atUint32 NAME_OFFSET                  = 0x08D4;
static const atUint32 RUPEE_COUNT_OFFSET           = 0x0A5E;
static const atUint32 AMMO_COUNT_OFFSET            = 0x0A60;
static const atUint32 MAX_HP_OFFSET                = 0x5302;
static const atUint32 SPAWN_HP_OFFSET              = 0x5304;
static const atUint32 CURRENT_HP_OFFSET            = 0x5306;
static const atUint32 ROOM_ID_OFFSET               = 0x5309;
static const atUint32 CURRENT_LOCATION_OFFSET      = 0x531C;
static const atUint32 CURRENT_AREA_OFFSET          = 0x533C;
static const atUint32 CURRENT_LOCATION_COPY_OFFSET = 0x535C;
static const atUint32 CHECKSUM_OFFSET              = 0x53BC;
static const atUint32 ISNEW_OFFSET                 = 0x53AD;

static const atUint32 SKIP_CHECKSUM_OFFSET         = 0x20;
}

union AmmoValues
{
    struct
    {
        atUint32 arrows : 7;
        atUint32 bombs  : 7;
        atUint32        : 9;
        atUint32 seeds  : 7;
        atUint32        : 2;
    };
    atUint32 value;
};

SkywardSwordQuest::SkywardSwordQuest(atUint8* data, atUint32 len)
    : ZQuestFile(ZQuestFile::SS, Endian::BigEndian, data, len),
      m_skipData(nullptr)
{
}

void SkywardSwordQuest::setSkipData(const atUint8* data)
{
    if (m_skipData)
    {
        delete[] m_skipData;
        m_skipData = nullptr;
    }

    m_skipData = (atUint8*)data;
}

atUint8* SkywardSwordQuest::skipData() const
{
    return m_skipData;
}

void SkywardSwordQuest::setPlayerName(const std::string& name)
{
    if (name.length() > 8)
        aDebug() << "WARNING: name cannot be greater than 8 characters, automatically truncating" << std::endl;

    std::vector<atUint16> val;
    utf8::utf8to16(name.begin(), name.end(), std::back_inserter(val));

    for (atUint32 i = 0; i < 8; i++)
    {
        atUint16& c = *(atUint16*)(m_data + priv::NAME_OFFSET + (i * 2));

        if (i >= val.size())
        {
            c = 0;
            continue;
        }

        c = val[i];
        utility::BigUint16(c);
    }
}

std::string SkywardSwordQuest::playerName() const
{
    std::vector<atUint16> val;

    for (atUint32 i = 0; i < 8; i++)
    {
        atUint16 c = *(atUint16*)(m_data + priv::NAME_OFFSET + (i * 2));

        if (c == 0)
            break;

        utility::BigUint16(c);
        val.push_back(c);
    }

    std::string ret;
    utf8::utf16to8(val.begin(), val.end(), std::back_inserter(ret));
    return std::string(ret.c_str());
}

void SkywardSwordQuest::setRupeeCount(atUint16 value)
{
    atUint16& tmp = *(atUint16*)(m_data + priv::RUPEE_COUNT_OFFSET);
    tmp = value;
    utility::BigUint16(tmp);
}

atUint16 SkywardSwordQuest::rupeeCount()
{
    atUint16 ret = *(atUint16*)(m_data + priv::RUPEE_COUNT_OFFSET);
    return utility::BigUint16(ret);
}

void SkywardSwordQuest::setAmmoCount(SkywardSwordQuest::AmmoType type, atUint32 count)
{
    AmmoValues& values = *(AmmoValues*)(m_data + priv::AMMO_COUNT_OFFSET);
    utility::BigUint32(values.value);

    switch (type)
    {
        case Arrows:
            values.arrows = count;
            break;

        case Bombs:
            values.bombs = count;
            break;

        case Seeds:
            values.seeds = count;
            break;
    }

    utility::BigUint32(values.value);
}

atUint32 SkywardSwordQuest::ammoCount(AmmoType type)
{
    AmmoValues values = *(AmmoValues*)(m_data + priv::AMMO_COUNT_OFFSET);
    utility::BigUint32(values.value);

    switch (type)
    {
        case Arrows:
            return values.arrows;

        case Bombs:
            return values.bombs;

        case Seeds:
            return values.seeds;

        default:
            return 0;
    }
}

void SkywardSwordQuest::setMaxHP(atUint16 val)
{
    *(atUint16*)(m_data + priv::MAX_HP_OFFSET) = utility::BigUint16(val);
}

atUint16 SkywardSwordQuest::maxHP()
{
    atUint16 ret = *(atUint16*)(m_data + priv::MAX_HP_OFFSET);
    return utility::BigUint16(ret);
}

float SkywardSwordQuest::maxHearts()
{
    return (maxHP() / 4.f);
}

void SkywardSwordQuest::setSpawnHP(atUint16 val)
{
    *(atUint16*)(m_data + priv::SPAWN_HP_OFFSET) = utility::BigUint16(val);
}

atUint16 SkywardSwordQuest::spawnHP()
{
    atUint16 ret = *(atUint16*)(m_data + priv::SPAWN_HP_OFFSET);
    return utility::BigUint16(ret);
}

float SkywardSwordQuest::spawnHearts()
{
    return (spawnHP() / 4.f);
}

void SkywardSwordQuest::setCurrentHP(atUint16 val)
{
    *(atUint16*)(m_data + priv::CURRENT_HP_OFFSET) = utility::BigUint16(val);
}

atUint16 SkywardSwordQuest::currentHP()
{
    atUint16 ret = *(atUint16*)(m_data + priv::CURRENT_HP_OFFSET);
    return utility::BigUint16(ret);
}

float SkywardSwordQuest::currentHearts()
{
    return (currentHP() / 4.f);
}

std::string SkywardSwordQuest::currentLocation()
{
    return std::string((char*)m_data + priv::CURRENT_LOCATION_OFFSET);
}

std::string SkywardSwordQuest::currentArea()
{
    return std::string((char*)m_data + priv::CURRENT_AREA_OFFSET);
}

std::string SkywardSwordQuest::currentLocationCopy()
{
    return std::string((char*)m_data + priv::CURRENT_LOCATION_COPY_OFFSET);
}

atUint32 SkywardSwordQuest::slotChecksum()
{
    atUint32 ret = *(atUint32*)(m_data + priv::CHECKSUM_OFFSET);
    utility::BigUint32(ret);

    return ret;
}

atUint32 SkywardSwordQuest::skipChecksum()
{
    atUint32 ret = *(atUint32*)(m_skipData + priv::SKIP_CHECKSUM_OFFSET);
    utility::BigUint32(ret);

    return ret;
}

void SkywardSwordQuest::fixChecksums()
{
    atUint32 checksum = Checksums::crc32(m_data, priv::CHECKSUM_OFFSET);
    utility::BigUint32(checksum);
    *(atUint32*)(m_data + priv::CHECKSUM_OFFSET) = checksum;

    checksum = Checksums::crc32(m_skipData, priv::SKIP_CHECKSUM_OFFSET);
    utility::BigUint32(checksum);
    *(atUint32*)(m_skipData + priv::SKIP_CHECKSUM_OFFSET) = checksum;
}

void SkywardSwordQuest::setNew(bool isNew)
{
    *(bool*)(m_data + priv::ISNEW_OFFSET) = isNew;
}

bool SkywardSwordQuest::isNew() const
{
    return *(bool*)(m_data + priv::ISNEW_OFFSET);
}

} // zelda
#endif // ATHENA_NO_SAVES