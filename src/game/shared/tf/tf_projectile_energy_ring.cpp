//====== Copyright � 1996-2013, Valve Corporation, All rights reserved. ========//
//
// Purpose: Flare used by the flaregun.
//
//=============================================================================//
#include "cbase.h"
#include "tf_projectile_energy_ring.h"
// Client specific.
#ifdef CLIENT_DLL
#include "c_tf_player.h"
#include "particles_new.h"
#include "iefx.h"
#include "dlight.h"
#include "tempent.h"
#include "c_te_legacytempents.h"
#else
#include "tf_player.h"
#include "tf_fx.h"
#include "ai_basenpc.h"
#include "effect_dispatch_data.h"
#include "collisionutils.h"
#include "tf_team.h"
#include "props.h"
#endif

#ifdef CLIENT_DLL
	extern ConVar lfe_muzzlelight;
#endif

#define TF_WEAPON_ENERGYRING_MODEL	"models/empty.mdl"

//=============================================================================
//
// Dragon's Fury Projectile
//

BEGIN_DATADESC( CTFProjectile_EnergyRing )
END_DATADESC()

LINK_ENTITY_TO_CLASS( tf_projectile_energy_ring, CTFProjectile_EnergyRing );
PRECACHE_REGISTER( tf_projectile_energy_ring );

IMPLEMENT_NETWORKCLASS_ALIASED( TFProjectile_EnergyRing, DT_TFProjectile_EnergyRing )
BEGIN_NETWORK_TABLE( CTFProjectile_EnergyRing, DT_TFProjectile_EnergyRing )
#ifdef GAME_DLL
	SendPropBool( SENDINFO( m_bCritical ) ),
#else
	RecvPropBool( RECVINFO( m_bCritical ) ),
#endif
END_NETWORK_TABLE()

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CTFProjectile_EnergyRing::CTFProjectile_EnergyRing()
{
#ifdef CLIENT_DLL
	m_pRing = NULL;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CTFProjectile_EnergyRing::~CTFProjectile_EnergyRing()
{
#ifdef CLIENT_DLL
	ParticleProp()->StopEmissionAndDestroyImmediately( m_pRing );
	m_pRing = NULL;
#else
	m_bCollideWithTeammates = false;
#endif
}

#ifdef GAME_DLL
//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::Precache()
{
	PrecacheModel( TF_WEAPON_ENERGYRING_MODEL );

	PrecacheParticleSystem( "drg_bison_projectile" );
	PrecacheParticleSystem( "drg_bison_projectile_crit" );
	PrecacheParticleSystem( "drg_bison_impact" );

	PrecacheScriptSound( "Weapon_Bison.ProjectileImpactWorld" );
	PrecacheScriptSound( "Weapon_Bison.ProjectileImpactFlesh" );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Spawn function
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::Spawn()
{
	SetModel( TF_WEAPON_ENERGYRING_MODEL );
	BaseClass::Spawn();

	float flRadius = 0.01f;
	UTIL_SetSize( this, -Vector( flRadius, flRadius, flRadius ), Vector( flRadius, flRadius, flRadius ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::RocketTouch( CBaseEntity *pOther )
{
	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		// Damage.
		CBaseEntity *pAttacker = GetOwnerEntity();
		IScorer *pScorerInterface = dynamic_cast<IScorer*>( pAttacker );
		if ( pScorerInterface )
			pAttacker = pScorerInterface->GetScorer();

		float flDamage = 20;
		int iDamageType = GetDamageType();

		CTFWeaponBase *pWeapon = ( CTFWeaponBase * )m_hLauncher.Get();

		CTakeDamageInfo info( GetOwnerEntity(), pAttacker, pWeapon, flDamage, iDamageType | DMG_PREVENT_PHYSICS_FORCE, TF_DMG_CUSTOM_PLASMA );
		pOther->TakeDamage( info );

		info.SetReportedPosition( pAttacker->GetAbsOrigin() );	

		// We collided with pOther, so try to find a place on their surface to show blood
		trace_t pTrace;
		Ray_t ray; ray.Init( WorldSpaceCenter(), pOther->WorldSpaceCenter() );
		UTIL_Portal_TraceRay( ray, /*MASK_SOLID*/ MASK_SHOT|CONTENTS_HITBOX, this, COLLISION_GROUP_DEBRIS, &pTrace );

		pOther->DispatchTraceAttack( info, GetAbsVelocity(), &pTrace );

		ApplyMultiDamage();
	}
	else
	{
		UTIL_Remove( this );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::SetScorer( CBaseEntity *pScorer )
{
	m_Scorer = pScorer;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBasePlayer *CTFProjectile_EnergyRing::GetScorer( void )
{
	return dynamic_cast<CBasePlayer *>( m_Scorer.Get() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::Explode( trace_t *pTrace, CBaseEntity *pOther )
{
	// Save this entity as enemy, they will take 100% damage.
	m_hEnemy = pOther;

	// Invisible.
	SetModelName( NULL_STRING );
	AddSolidFlags( FSOLID_NOT_SOLID );
	m_takedamage = DAMAGE_NO;

	// Pull out a bit.
	if ( pTrace->fraction != 1.0 )
		SetAbsOrigin( pTrace->endpos + ( pTrace->plane.normal * 1.0f ) );

	// Remove.
	UTIL_Remove( this );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTFProjectile_EnergyRing *CTFProjectile_EnergyRing::Create( CBaseEntity *pWeapon, const Vector &vecOrigin, const QAngle &vecAngles, CBaseEntity *pOwner, CBaseEntity *pScorer )
{
	CTFProjectile_EnergyRing * pRing = static_cast<CTFProjectile_EnergyRing*>( CBaseEntity::CreateNoSpawn( "tf_projectile_energy_ring", vecOrigin, vecAngles, pOwner ) );
	if ( pRing )
	{
		// Set team.
		pRing->ChangeTeam( pOwner->GetTeamNumber() );

		// Set scorer.
		pRing->SetScorer( pScorer );

		// Set firing weapon.
		pRing->SetLauncher( pWeapon );

		// Spawn.
		DispatchSpawn(pRing);

		// Setup the initial velocity.
		Vector vecForward, vecRight, vecUp;
		AngleVectors( vecAngles, &vecForward, &vecRight, &vecUp );

		float flVelocity = 1200.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( pWeapon, flVelocity, mult_projectile_speed );

		Vector vecVelocity = vecForward * flVelocity;
		pRing->SetAbsVelocity( vecVelocity );
		pRing->SetupInitialTransmittedGrenadeVelocity( vecVelocity );

		// Setup the initial angles.
		QAngle angles;
		VectorAngles( vecVelocity, angles );
		pRing->SetAbsAngles( angles );
		
		float flGravity = 0.0f;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( pWeapon, flGravity, mod_rocket_gravity );
		if ( flGravity )
		{
			pRing->SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
			pRing->SetGravity( flGravity );
		}

		return pRing;
	}

	return pRing;
}
#else

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		CreateTrails();
		CreateLightEffects();
	}

	// Watch team changes and change trail accordingly.
	if ( m_iOldTeamNum && m_iOldTeamNum != m_iTeamNum )
	{
		ParticleProp()->StopEmission();
		CreateTrails();
		CreateLightEffects();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::CreateTrails( void )
{
	if ( IsDormant() )
		return;

	const char *pszEffectName = m_bCritical ? "drg_bison_projectile_crit" : "drg_bison_projectile";

	m_pRing = ParticleProp()->Create( pszEffectName, PATTACH_ABSORIGIN_FOLLOW );

	Vector vecColor;

	switch ( GetTeamNumber() )
	{
		case TF_TEAM_RED:
			vecColor.Init( 255, -255, -255 );
			break;
		case TF_TEAM_BLUE:
			vecColor.Init( -255, -255, 255 );
			break;
	}

	m_pRing->SetControlPoint( 2, vecColor );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFProjectile_EnergyRing::CreateLightEffects( void )
{
	// Handle the dynamic light
	if (lfe_muzzlelight.GetBool())
	{
		AddEffects( EF_DIMLIGHT );

		dlight_t *dl;
		if ( IsEffectActive( EF_DIMLIGHT ) )
		{	
			dl = effects->CL_AllocDlight( LIGHT_INDEX_TE_DYNAMIC + index );
			dl->origin = GetAbsOrigin();
			switch ( GetTeamNumber() )
			{
			case TF_TEAM_RED:
				if ( !m_bCritical )
				{	dl->color.r = 255; dl->color.g = 30; dl->color.b = 10; }
				else
				{	dl->color.r = 255; dl->color.g = 10; dl->color.b = 10; }
				break;

			case TF_TEAM_BLUE:
				if ( !m_bCritical )
				{	dl->color.r = 10; dl->color.g = 30; dl->color.b = 255; }
				else
				{	dl->color.r = 10; dl->color.g = 10; dl->color.b = 255; }
				break;
			}
			dl->radius = 256.0f;
			dl->die = gpGlobals->curtime + 0.1;

			tempents->RocketFlare( GetAbsOrigin() );
		}
	}
}
#endif