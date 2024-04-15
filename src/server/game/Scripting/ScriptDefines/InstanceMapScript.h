/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright
 * information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCRIPT_OBJECT_INSTANCE_MAP_SCRIPT_H_
#define SCRIPT_OBJECT_INSTANCE_MAP_SCRIPT_H_

#include "ScriptObject.h"

class InstanceMapScript : public ScriptObject, public MapScript<InstanceMap> {
protected:
    InstanceMapScript(const char* name, uint32 mapId);

public:
    [[nodiscard]] bool IsDatabaseBound() const override { return true; }

    void checkValidity() override;

    // Gets an InstanceScript object for this instance.
    virtual InstanceScript* GetInstanceScript(InstanceMap* /*map*/) const
    {
        return nullptr;
    }
};

#endif
