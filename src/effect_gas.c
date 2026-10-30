// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An effect def's gases checked against its emitters: the gases followed
// and splatted into, the gases' forces, and each liquid gas's liquid.

#include "effect_gas.h"

#include "force.h"
#include "refusal.h"

// Whether an emitter's splat into a gas is in range for an effect of
// gasCount gases; naming the field it refuses below at.
static mvfxResult CheckSplat(const mvfxFieldPath* at, const mvfxGasSplat* splat, int32_t gasCount)
{
    if (!splat->enable)
    {
        return mvfx_success;
    }
    if (splat->gas < 0 || splat->gas >= gasCount)
    {
        return mvfxRefuseField(at, MVFX_TAG_GAS_SPLAT_GAS, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_GAS_SPLAT_DRAG,
                                       mvfxUpToBound(splat->drag, 0.0f, MVFX_MAX_DRAG));
    const float amounts[3] = {splat->density, splat->temperature, splat->fuel};
    const uint32_t tags[3] = {MVFX_TAG_GAS_SPLAT_DENSITY, MVFX_TAG_GAS_SPLAT_TEMPERATURE,
                              MVFX_TAG_GAS_SPLAT_FUEL};
    for (int32_t k = 0; valid == mvfx_success && k < 3; ++k)
    {
        valid = mvfxRefuseField(at, tags[k],
                                mvfxBounded(amounts[k], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
    }
    return valid;
}

int32_t mvfxLiquidOf(const mvfxEffectDef* def, int32_t g)
{
    int32_t found = -1;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxLiquidDef* liquid = &def->emitters[i].liquid;
        if (liquid->enable && liquid->solver == mvfx_liquidGrid && liquid->gas == g)
        {
            found = found == -1 ? i : -2;
        }
    }
    return found;
}

// Whether every grid liquid names a liquid gas of the effect, each liquid
// gas is named by exactly one, no emitter splats into a liquid or surface
// gas, and each surface gas names an emitter of the effect; naming the
// field it refuses below root, a liquid gas's kind for one named by none
// or by two.
static mvfxResult CheckLiquidGases(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxEmitterDef* emitter = &def->emitters[i];
        const mvfxLiquidDef* liquid = &emitter->liquid;
        mvfxFieldPath at = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_EMITTERS, i);
        bool grid = liquid->enable && liquid->solver == mvfx_liquidGrid;
        if (grid && (liquid->gas < 0 || liquid->gas >= def->gasCount ||
                     def->gases[liquid->gas].kind != mvfx_gasLiquid))
        {
            mvfxFieldPath liquidAt = mvfxFieldIn(&at, MVFX_TAG_EMITTER_DEF_LIQUID, -1);
            return mvfxRefuseField(&liquidAt, MVFX_TAG_LIQUID_DEF_GAS, mvfx_errorInvalid);
        }
        if (emitter->gasSplat.enable && def->gases[emitter->gasSplat.gas].kind != mvfx_gasSmoke)
        {
            mvfxFieldPath splat = mvfxFieldIn(&at, MVFX_TAG_EMITTER_DEF_GAS_SPLAT, -1);
            return mvfxRefuseField(&splat, MVFX_TAG_GAS_SPLAT_GAS, mvfx_errorInvalid);
        }
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        const mvfxGasDef* gas = &def->gases[g];
        mvfxFieldPath at = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_GASES, g);
        if (gas->kind == mvfx_gasLiquid && mvfxLiquidOf(def, g) < 0)
        {
            return mvfxRefuseField(&at, MVFX_TAG_GAS_DEF_KIND, mvfx_errorInvalid);
        }
        if (gas->kind == mvfx_gasSurface &&
            (gas->surface.emitter < 0 || gas->surface.emitter >= def->emitterCount))
        {
            mvfxFieldPath surface = mvfxFieldIn(&at, MVFX_TAG_GAS_DEF_SURFACE, -1);
            return mvfxRefuseField(&surface, MVFX_TAG_GAS_SURFACE_EMITTER, mvfx_errorInvalid);
        }
    }
    return mvfx_success;
}

// Whether every gas an emitter's forces follow or it splats into is one
// of the effect's and none a surface, and no gas's force follows a gas.
static mvfxResult CheckGasLinks(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        for (int32_t f = 0; f < def->emitters[i].forceCount; ++f)
        {
            const mvfxForce* force = &def->emitters[i].forces[f];
            if (force->kind == mvfx_forceGas &&
                (force->gas >= def->gasCount || def->gases[force->gas].kind == mvfx_gasSurface))
            {
                mvfxFieldPath emitter = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_EMITTERS, i);
                mvfxFieldPath at = mvfxFieldIn(&emitter, MVFX_TAG_EMITTER_DEF_FORCES, f);
                return mvfxRefuseField(&at, MVFX_TAG_FORCE_GAS, mvfx_errorInvalid);
            }
        }
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        for (int32_t f = 0; f < def->gases[g].forceCount; ++f)
        {
            if (def->gases[g].forces[f].kind == mvfx_forceGas)
            {
                mvfxFieldPath gas = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_GASES, g);
                mvfxFieldPath at = mvfxFieldIn(&gas, MVFX_TAG_GAS_DEF_FORCES, f);
                return mvfxRefuseField(&at, MVFX_TAG_FORCE_KIND, mvfx_errorInvalid);
            }
        }
    }
    // The splats first: the liquid check reads the gas each splats into.
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        mvfxFieldPath emitter = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_EMITTERS, i);
        mvfxFieldPath at = mvfxFieldIn(&emitter, MVFX_TAG_EMITTER_DEF_GAS_SPLAT, -1);
        mvfxResult splat = CheckSplat(&at, &def->emitters[i].gasSplat, def->gasCount);
        if (splat != mvfx_success)
        {
            return splat;
        }
    }
    return CheckLiquidGases(root, def);
}

// Whether a gas's forces are in range for the effect's parameters.
static mvfxResult CheckGasForces(const mvfxFieldPath* at, const mvfxGasDef* gas,
                                 const mvfxEffectDef* def)
{
    if (gas->forceCount < 0 || (gas->forceCount > 0 && gas->forces == nullptr))
    {
        return mvfxRefuseField(at, MVFX_TAG_GAS_DEF_FORCES, mvfx_errorInvalid);
    }
    if (gas->forceCount > MVFX_MAX_FORCES)
    {
        return mvfxRefuseField(at, MVFX_TAG_GAS_DEF_FORCES, mvfx_errorLimit);
    }
    int32_t parameters = def->parameterCount >= 0 && def->parameterCount <= MVFX_MAX_PARAMETERS
                             ? def->parameterCount
                             : 0;
    for (int32_t f = 0; f < gas->forceCount; ++f)
    {
        mvfxFieldPath force = mvfxFieldIn(at, MVFX_TAG_GAS_DEF_FORCES, f);
        mvfxResult checked = mvfxCheckForce(&force, &gas->forces[f], parameters);
        if (checked != mvfx_success)
        {
            return checked;
        }
    }
    return mvfx_success;
}

mvfxResult mvfxCheckEffectGases(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        mvfxFieldPath at = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_GASES, g);
        mvfxResult forces = CheckGasForces(&at, &def->gases[g], def);
        if (forces != mvfx_success)
        {
            return forces;
        }
    }
    return CheckGasLinks(root, def);
}
