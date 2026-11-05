/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_B4AssetIO.h
 *  \ingroup gamelogic
 *
 *  Shared asset serialization used by the JSON and binary save nodes.
 */

#pragma once

#include <string>

class KX_ExecutionContext;

namespace KXJson {
struct Value;
}

/** Build the save schema root (`version` + `assets`) used by every format. */
bool B4SerializeAssetsValue(KX_ExecutionContext &ctx,
                            KXJson::Value *out,
                            std::string *error);

/** JSON text encoding of #B4SerializeAssetsValue. */
bool B4SerializeAssets(KX_ExecutionContext &ctx, std::string *out, std::string *error);

/** Apply a save schema root to the asset registry. */
bool B4ApplyAssets(KX_ExecutionContext &ctx, const KXJson::Value &root, std::string *error);
