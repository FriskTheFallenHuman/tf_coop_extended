//========= Copyright Valve Corporation, All rights reserved. =================//
//
// Purpose: random useless weapon lead to dev abuse
//
//=============================================================================//

#include "cbase.h"
#include "tf_weapon_physgun.h"

#ifdef CLIENT_DLL
	#include "c_tf_player.h"
	#include "c_baseobject.h"
	#include "vcollide_parse.h"
	#include "engine/ivdebugoverlay.h"
	#include "iviewrender.h"
	#include "view_shared.h"
	#include "view.h"
	#include "c_te_effect_dispatch.h"
	#include "model_types.h"
	#include "clienteffectprecachesystem.h"
	#include "beamdraw.h"
	#include "dlight.h"
	#include "particles_simple.h"
	#include "r_efx.h"
#else
	#include "tf_player.h"
	#include "tf_obj.h"
	#include "soundent.h"
	#include "ndebugoverlay.h"
	#include "ai_basenpc.h"
	#include "player_pickup.h"
	#include "physics_prop_ragdoll.h"
	#include "globalstate.h"
	#include "props.h"
	#include "te_effect_dispatch.h"
	#include "util.h"
	#include "ilagcompensationmanager.h"
#endif

#include "gamerules.h"
#include "engine/IEngineSound.h"
#include "vphysics/constraints.h"
#include "IEffects.h"
#include "shake.h"
#include "Sprite.h"
#include "beam_shared.h"
#include "movevars_shared.h"
#include "vphysics/friction.h"
#include "debugoverlay_shared.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "tf_weapon_portalgun.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
extern ConVar lfe_muzzlelight;
#endif

#define	SPRITE_SCALE	128.0f

// new commands
//ConVar physgun_allow_npc( "physgun_allow_npc", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Enable NPCs grab for Physics Gun \nWARNING: MAY CRASH THE GAME" );
//ConVar physgun_allow_building( "physgun_allow_building", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "Enable Buildings grab for Physics Gun" );
//ConVar physgun_allow_ragdoll( "physgun_allow_ragdoll", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Enable Ragdolls grab for Physics Gun" );

 //original commands
 /*
ConVar phys_gunmass("phys_gunmass", "200");
ConVar phys_gunvel("phys_gunvel", "400");
ConVar phys_gunforce("phys_gunforce", "5e5" );
ConVar phys_guntorque("phys_guntorque", "100" );
ConVar phys_gunglueradius("phys_gunglueradius", "128" );
*/

static int g_physgunBeam1;
static int g_physgunBeam;
static int g_physgunGlow;
#define PHYSGUN_BEAM_SPRITE1	"sprites/physbeam1.vmt"
#define PHYSGUN_BEAM_SPRITE		"sprites/physbeam.vmt"
#define PHYSGUN_BEAM_GLOW		"sprites/physglow.vmt"

#define	PHYSGUN_SKIN	1

#ifdef CLIENT_DLL
CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectGravityGun )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_SPRITE1 )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_GLOW )
CLIENTEFFECT_REGISTER_END()
#endif

IPhysicsObject *GetPhysObjFromPhysicsBone( CBaseEntity *pEntity, short physicsbone )
{
	CBaseAnimating *pModel = static_cast< CBaseAnimating * >( pEntity );
	if ( pModel != NULL )
	{
		IPhysicsObject	*pPhysicsObject = NULL;
		
		//Find the real object we hit.
		if( physicsbone >= 0 )
		{
#ifdef CLIENT_DLL
			if ( pModel->m_pRagdoll )
			{
				CRagdoll *pCRagdoll = dynamic_cast < CRagdoll * > ( pModel->m_pRagdoll );
#else
				// Affect the object
				CRagdollProp *pCRagdoll = dynamic_cast<CRagdollProp*>( pEntity );
#endif
				if ( pCRagdoll )
				{
					ragdoll_t *pRagdollT = pCRagdoll->GetRagdoll();

					if ( physicsbone < pRagdollT->listCount )
					{
						pPhysicsObject = pRagdollT->list[physicsbone].pObject;
					}
					return pPhysicsObject;
				}
#ifdef CLIENT_DLL
			}
#endif
		}
	}

	return pEntity->VPhysicsGetObject();
}


BEGIN_SIMPLE_DATADESC( CGravControllerPoint )

	DEFINE_FIELD( m_localPosition,		FIELD_VECTOR ),
	DEFINE_FIELD( m_targetPosition,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_worldPosition,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_saveDamping,			FIELD_FLOAT ),
	DEFINE_FIELD( m_saveMass,			FIELD_FLOAT ),
	DEFINE_FIELD( m_maxAcceleration,		FIELD_FLOAT ),
	DEFINE_FIELD( m_maxAngularAcceleration,	FIELD_VECTOR ),
	DEFINE_FIELD( m_attachedEntity,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_attachedPhysicsBone,		FIELD_SHORT ),
	DEFINE_FIELD( m_targetRotation,		FIELD_VECTOR ),
	DEFINE_FIELD( m_timeToArrive,			FIELD_FLOAT ),
	DEFINE_FIELD( m_vecRotatedCarryAngles, FIELD_VECTOR ),
	DEFINE_FIELD( m_bHasRotatedCarryAngles, FIELD_BOOLEAN ),

	// Physptrs can't be saved in embedded classes... this is to silence classcheck
	// DEFINE_PHYSPTR( m_controller ),

END_DATADESC()

//=============================================================================
//
// Laser Dot functions.
//

IMPLEMENT_NETWORKCLASS_ALIASED( PhysgunDot, DT_PhysDot )

BEGIN_NETWORK_TABLE( CPhysgunDot, DT_PhysDot )
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS( physgun_dot, CPhysgunDot );

BEGIN_DATADESC( CPhysgunDot )
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor.
//-----------------------------------------------------------------------------
CPhysgunDot::CPhysgunDot( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Destructor.
//-----------------------------------------------------------------------------
CPhysgunDot::~CPhysgunDot( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CPhysgunDot *CPhysgunDot::Create( const Vector &origin, CBaseEntity *pOwner, bool bVisibleDot )
{
#ifdef GAME_DLL

	// Create the sniper dot entity.
	CPhysgunDot *pDot = static_cast<CPhysgunDot*>( CBaseEntity::Create( "physgun_dot", origin, QAngle( 0.0f, 0.0f, 0.0f ) ) );
	if ( !pDot )
		return NULL;

	//Create the graphic
	pDot->SetMoveType( MOVETYPE_NONE );
	pDot->AddSolidFlags( FSOLID_NOT_SOLID );
	pDot->AddEffects( EF_NOSHADOW );
	UTIL_SetSize( pDot, -Vector( 4.0f, 4.0f, 4.0f ), Vector( 4.0f, 4.0f, 4.0f ) );

	// Set owner.
	pDot->SetOwnerEntity( pOwner );

	// Force updates even though we don't have a model.
	pDot->AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	return pDot;

#else
	return NULL;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPhysgunDot::Update( const Vector &vecOrigin )
{
	SetAbsOrigin( vecOrigin );
}

CGravControllerPoint::CGravControllerPoint( void )
{
	m_shadow.dampFactor = 0.8;
	m_shadow.teleportDistance = 0;
	// make this controller really stiff!
	m_shadow.maxSpeed = 5000;
	m_shadow.maxAngular = m_shadow.maxSpeed;
	m_shadow.maxDampSpeed = m_shadow.maxSpeed*2;
	m_shadow.maxDampAngular = m_shadow.maxAngular*2;
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	// adnan
	// initialize our added vars
	m_vecRotatedCarryAngles = vec3_angle;
	m_bHasRotatedCarryAngles = false;
	// end adnan
}

CGravControllerPoint::~CGravControllerPoint( void )
{
	DetachEntity();
}


QAngle CGravControllerPoint::TransformAnglesToPlayerSpace( const QAngle &anglesIn, CTFPlayer *pTFPlayer )
{
	matrix3x4_t test;
	QAngle angleTest = pTFPlayer->EyeAngles();
	angleTest.x = 0;
	AngleMatrix( angleTest, test );
	return TransformAnglesToLocalSpace( anglesIn, test );
}

QAngle CGravControllerPoint::TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CTFPlayer *pTFPlayer )
{
	matrix3x4_t test;
	QAngle angleTest = pTFPlayer->EyeAngles();
	angleTest.x = 0;
	AngleMatrix( angleTest, test );
	return TransformAnglesToWorldSpace( anglesIn, test );
}


void CGravControllerPoint::AttachEntity( CTFPlayer *pTFPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &vGrabPosition )
{
	m_attachedEntity = pEntity;
	#ifdef GAME_DLL
	CBaseAnimating *pModel = dynamic_cast< CBaseAnimating * >( pEntity );
	if ( pModel )
		pModel->AddGlowEffect(); //Make it glow
	#endif
	m_attachedPhysicsBone = physicsbone;
	pPhys->WorldToLocal( &m_localPosition, vGrabPosition );
	m_worldPosition = vGrabPosition;
	pPhys->GetDamping( NULL, &m_saveDamping );
	m_saveMass = pPhys->GetMass();
	float damping = 2;
	pPhys->SetDamping( NULL, &damping );
	//pPhys->SetMass( 50000 );
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhys, true );
	Vector position;
	QAngle angles;
	pPhys->GetPosition( &position, &angles );

	SetTargetPosition( vGrabPosition, angles );
	m_targetRotation = TransformAnglesToPlayerSpace( angles, pTFPlayer );
	// adnan
	// we need to grab the preferred/non preferred carry angles here for the rotatedcarryangles
	m_vecRotatedCarryAngles = m_targetRotation;
	// end adnan
}

void CGravControllerPoint::DetachEntity( void )
{
	CBaseEntity *pEntity = m_attachedEntity;
	if ( pEntity )
	{
		#ifdef GAME_DLL
		CBaseAnimating *pModel = dynamic_cast< CBaseAnimating * >( pEntity );
		if ( pModel )
			pModel->RemoveGlowEffect();
		#endif
		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
		if ( pPhys )
		{
			// on the odd chance that it's gone to sleep while under anti-gravity
			pPhys->Wake();
			pPhys->SetDamping( NULL, &m_saveDamping );
			//pPhys->SetMass( m_saveMass );
		}
	}
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	if ( physenv )
	{
		physenv->DestroyMotionController( m_controller );
	}
	m_controller = NULL;

	// UNDONE: Does this help the networking?
	m_targetPosition = vec3_origin;
	m_worldPosition = vec3_origin;
}

void AxisAngleQAngle( const Vector &axis, float angle, QAngle &outAngles )
{
	// map back to HL rotation axes
	outAngles.z = axis.x * angle;
	outAngles.x = axis.y * angle;
	outAngles.y = axis.z * angle;
}

IMotionEvent::simresult_e CGravControllerPoint::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	hlshadowcontrol_params_t shadowParams = m_shadow;
#ifndef CLIENT_DLL
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, m_timeToArrive, deltaTime );
#else
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, (TICK_INTERVAL*2), deltaTime );
#endif
	
	linear.Init();
	angular.Init();

	return SIM_LOCAL_ACCELERATION;
}

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponGravityGun, DT_WeaponGravityGun )

BEGIN_NETWORK_TABLE( CWeaponGravityGun, DT_WeaponGravityGun )
#ifdef CLIENT_DLL
	RecvPropEHandle( RECVINFO( m_hObject ) ),
	RecvPropInt( RECVINFO( m_physicsBone ) ),
	RecvPropVector( RECVINFO( m_targetPosition ) ),
	RecvPropVector( RECVINFO( m_worldPosition ) ),
	RecvPropInt( RECVINFO(m_active) ),
	// adnan
	// also receive if we're rotating what we're holding (by pressing use)
	RecvPropBool( RECVINFO( m_bIsCurrentlyRotating ) ),
	// end adnan
#else
	SendPropEHandle( SENDINFO( m_hObject ) ),
	SendPropInt( SENDINFO( m_physicsBone ) ),
	SendPropVector(SENDINFO( m_targetPosition ), -1, SPROP_COORD),
	SendPropVector(SENDINFO( m_worldPosition ), -1, SPROP_COORD),
	SendPropInt( SENDINFO(m_active), 1, SPROP_UNSIGNED ),
	// adnan
	// need to seind if we're rotating what we're holding
	SendPropBool( SENDINFO( m_bIsCurrentlyRotating ) ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CWeaponGravityGun )
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( tf_weapon_physgun, CWeaponGravityGun );
PRECACHE_WEAPON_REGISTER( tf_weapon_physgun );

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CWeaponGravityGun )

	DEFINE_FIELD( m_active,				FIELD_INTEGER ),
	DEFINE_FIELD( m_useDown,				FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hObject,				FIELD_EHANDLE ),
	DEFINE_FIELD( m_physicsBone,				FIELD_INTEGER ),
	DEFINE_FIELD( m_distance,			FIELD_FLOAT ),
	DEFINE_FIELD( m_movementLength,		FIELD_FLOAT ),
	DEFINE_FIELD( m_soundState,			FIELD_INTEGER ),
	DEFINE_FIELD( m_originalObjectPosition,	FIELD_POSITION_VECTOR ),
	// adnan
	DEFINE_FIELD( m_bIsCurrentlyRotating, FIELD_BOOLEAN ),
	// end adnan
	DEFINE_SOUNDPATCH( m_sndMotor ),
	DEFINE_SOUNDPATCH( m_sndLockedOn ),
	DEFINE_SOUNDPATCH( m_sndLightObject ),
	DEFINE_SOUNDPATCH( m_sndHeavyObject ),
	DEFINE_EMBEDDED( m_gravCallback ),
	// Physptrs can't be saved in embedded classes..
	//DEFINE_PHYSPTR( m_gravCallback.m_controller ),

END_DATADESC()


enum physgun_soundstate { SS_SCANNING, SS_LOCKEDON };
enum physgun_soundIndex { SI_LOCKEDON = 0, SI_SCANNING = 1, SI_LIGHTOBJECT = 2, SI_HEAVYOBJECT = 3, SI_ON, SI_OFF };


//=========================================================
//=========================================================

CWeaponGravityGun::CWeaponGravityGun()
{
	m_active = false;
	m_bFiresUnderwater = true;
	m_bInWeapon1 = false;
	m_bInWeapon2 = false;
#ifdef GAME_DLL
	m_hPhysgunDot = NULL;
#endif
}

CWeaponGravityGun::~CWeaponGravityGun()
{
	DestroyPhysgunDot();
}

//-----------------------------------------------------------------------------
// On Remove
//-----------------------------------------------------------------------------
void CWeaponGravityGun::UpdateOnRemove(void)
{
	EffectDestroy();
	SoundDestroy();
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// adnan
// want to add an angles modifier key
bool CGravControllerPoint::UpdateObject( CTFPlayer *pTFPlayer, CBaseEntity *pEntity )
{
	IPhysicsObject *pPhysics = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
	if ( !pEntity || !pPhysics )
	{
		return false;
	}
	// adnan
	// if we've been rotating it, set it to its proper new angles (change m_attachedAnglesPlayerSpace while modifier)
	//Pickup_GetRotatedCarryAngles( pEntity, pTFPlayer, pTFPlayer->EntityToWorldTransform(), angles );
	// added the ... && (mousedx | mousedy) so we dont have to calculate if no mouse movement
	// UPDATE: m_vecRotatedCarryAngles has become a temp variable... can be cleaned up by using actual temp vars
#ifdef CLIENT_DLL
	if( m_bHasRotatedCarryAngles && (pTFPlayer->m_pCurrentCommand->mousedx || pTFPlayer->m_pCurrentCommand->mousedy) )
#else
	if( m_bHasRotatedCarryAngles && (pTFPlayer->GetCurrentCommand()->mousedx || pTFPlayer->GetCurrentCommand()->mousedy) )
#endif
	{
		// method II: relative orientation
		VMatrix vDeltaRotation, vCurrentRotation, vNewRotation;
		
		MatrixFromAngles( m_targetRotation, vCurrentRotation );

#ifdef CLIENT_DLL
		m_vecRotatedCarryAngles[YAW] = pTFPlayer->m_pCurrentCommand->mousedx*0.05;
		m_vecRotatedCarryAngles[PITCH] = pTFPlayer->m_pCurrentCommand->mousedy*-0.05;
#else
		m_vecRotatedCarryAngles[YAW] = pTFPlayer->GetCurrentCommand()->mousedx*0.05;
		m_vecRotatedCarryAngles[PITCH] = pTFPlayer->GetCurrentCommand()->mousedy*-0.05;
#endif
		m_vecRotatedCarryAngles[ROLL] = 0;
		MatrixFromAngles( m_vecRotatedCarryAngles, vDeltaRotation );

		MatrixMultiply(vDeltaRotation, vCurrentRotation, vNewRotation);
		MatrixToAngles( vNewRotation, m_targetRotation );
	}
	// end adnan

	SetTargetPosition( m_targetPosition, m_targetRotation );

	return true;
}

void CGravControllerPoint::SetTargetPosition( const Vector &target, const QAngle &targetOrientation )
{
	m_shadow.targetPosition = target;
	m_shadow.targetRotation = targetOrientation;

	m_timeToArrive = gpGlobals->frametime;

	CBaseEntity *pAttached = m_attachedEntity;
	if ( pAttached )
	{
		IPhysicsObject *pObj = GetPhysObjFromPhysicsBone( pAttached, m_attachedPhysicsBone );

		if ( pObj != NULL )
		{
			pObj->Wake();
		}
		else
		{
			DetachEntity();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allow weapons to override mouse input to viewangles (for orbiting)
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::OverrideViewAngles( void )
{
	CTFPlayer *pPlayer = GetTFPlayerOwner();
	
	if(!pPlayer)
		return true;

	if (m_bIsCurrentlyRotating) {
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CWeaponGravityGun::Spawn( )
{
	BaseClass::Spawn();
//	SetModel( GetWorldModel() );

	// The physgun uses a different skin
	m_nSkin = PHYSGUN_SKIN;

	FallInit();
}

void CWeaponGravityGun::OnRestore( void )
{
	BaseClass::OnRestore();

	if ( m_gravCallback.m_controller )
	{
		m_gravCallback.m_controller->SetEventHandler( &m_gravCallback );
	}
}


//=========================================================
//=========================================================
void CWeaponGravityGun::Precache( void )
{
	BaseClass::Precache();

	g_physgunBeam1 = PrecacheModel(PHYSGUN_BEAM_SPRITE1);
	g_physgunBeam = PrecacheModel(PHYSGUN_BEAM_SPRITE);
	g_physgunGlow = PrecacheModel(PHYSGUN_BEAM_GLOW);

	PrecacheScriptSound( "Weapon_Physgun.Scanning" );
	PrecacheScriptSound( "Weapon_Physgun.LockedOn" );
	PrecacheScriptSound( "Weapon_Physgun.Scanning" );
	PrecacheScriptSound( "Weapon_Physgun.LightObject" );
	PrecacheScriptSound( "Weapon_Physgun.HeavyObject" );
}

void CWeaponGravityGun::EffectCreate( void )
{
#ifdef GAME_DLL
	lagcompensation->StartLagCompensation( GetTFPlayerOwner(), LAG_COMPENSATE_BOUNDS );
#endif
	EffectUpdate();
	m_active = true;
}


// Andrew; added so we can trace both in EffectUpdate and DrawModel with the same results
void CWeaponGravityGun::TraceLine( trace_t *ptr )
{
	CTFPlayer *pOwner = GetTFPlayerOwner();
	if ( !pOwner )
		return;

	Vector start, forward, right;
	pOwner->EyeVectors( &forward, &right, NULL );

	start = pOwner->Weapon_ShootPosition();
	Vector end = start + forward * 4096;

	Ray_t ray; ray.Init( start, end );
	UTIL_Portal_TraceRay( ray, MASK_SHOT|CONTENTS_GRATE, pOwner, COLLISION_GROUP_NONE, ptr );

#ifdef GAME_DLL
	if ( m_hPhysgunDot )
		m_hPhysgunDot->Update( end );
#endif
}


void CWeaponGravityGun::EffectUpdate( void )
{
	Vector start, forward, right;
	trace_t tr;

	CTFPlayer *pOwner = GetTFPlayerOwner();
	if ( !pOwner )
		return;

	pOwner->EyeVectors( &forward, &right, NULL );

	start = pOwner->Weapon_ShootPosition();

	TraceLine( &tr );
	Vector end = tr.endpos;
	float distance = tr.fraction * 4096;
#ifdef GAME_DLL
	CreatePhysgunDot();
#endif
	if ( m_hObject == NULL && tr.DidHitNonWorldEntity() && !tr.m_pEnt->IsPlayer() )
	{
		CBaseEntity *pEntity = tr.m_pEnt;
		AttachObject( pEntity, tr.physicsbone, start, tr.endpos, distance );
	}

	// Add the incremental player yaw to the target transform
	QAngle angles = m_gravCallback.TransformAnglesFromPlayerSpace( m_gravCallback.m_targetRotation, pOwner );

	CBaseEntity *pObject = m_hObject;
	if ( pObject )
	{
		#ifdef CLIENT_DLL
		// Handle the muzzle light
		if (lfe_muzzlelight.GetBool())
		{
			dlight_t *dlobj = effects->CL_AllocDlight(LIGHT_INDEX_MUZZLEFLASH + index);
			dlobj->origin = pObject->GetAbsOrigin();
			dlobj->color.r = 47;
			dlobj->color.g = 141;
			dlobj->color.b = 255;
			dlobj->die = gpGlobals->curtime + 0.01f;
			dlobj->radius = 350.f;
			dlobj->decay = 512.0f;
			dlobj->style = 0;
		}
		#endif
		if ( m_useDown )
		{
			if ( pOwner->m_afButtonPressed & IN_ATTACK2 )
			{
				m_useDown = false;

				IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pObject, m_physicsBone );
				if ( pPhys )
				{
#ifdef GAME_DLL
					if ( !pObject->ClassMatches( "prop_vehicle_jeep" ) )
					{
						if ( pPhys->IsMotionEnabled() )
							pPhys->EnableMotion( false );
						else
							pPhys->EnableMotion( true );
					}
#endif
				}
			}
		}
		else 
		{
			if ( pOwner->m_afButtonPressed & IN_ATTACK2 )
			{
				m_useDown = true;	
			}
		}

		if ( m_useDown )
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, true );
#endif
			if ( pOwner->m_nButtons & IN_FORWARD )
			{
				m_distance = Approach( 1024, m_distance, gpGlobals->frametime * 100 );
			}
			if ( pOwner->m_nButtons & IN_BACK )
			{
				m_distance = Approach( 40, m_distance, gpGlobals->frametime * 100 );
			}
		}

		if ( pOwner->m_nButtons & IN_WEAPON1 )
		{
			m_distance = Approach( 1024, m_distance, m_distance * 0.1 );
#ifdef CLIENT_DLL
			if ( gpGlobals->maxClients > 1 )
			{
				gHUD.m_bSkipClear = false;
			}
#endif
		}
		if ( pOwner->m_nButtons & IN_WEAPON2 )
		{
			m_distance = Approach( 40, m_distance, m_distance * 0.1 );
#ifdef CLIENT_DLL
			if ( gpGlobals->maxClients > 1 )
			{
				gHUD.m_bSkipClear = false;
			}
#endif
		}

		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pObject, m_physicsBone );
		if ( pPhys )
		{
			if ( pPhys->IsAsleep() )
			{
				// on the odd chance that it's gone to sleep while under anti-gravity
				pPhys->Wake();
			}

			Vector newPosition = start + forward * m_distance;
			Vector offset;
			pPhys->LocalToWorld( &offset, m_worldPosition );
			Vector vecOrigin;
			pPhys->GetPosition( &vecOrigin, NULL );
			m_gravCallback.SetTargetPosition( newPosition + (vecOrigin - offset), angles );
			Vector dir = (newPosition - pObject->GetLocalOrigin());
			m_movementLength = dir.Length();
		}
	}
	else
	{
		m_targetPosition = end;
		m_gravCallback.SetTargetPosition( end, m_gravCallback.m_targetRotation );
	}
}

void CWeaponGravityGun::SoundCreate( void )
{
	m_soundState = SS_SCANNING;
	SoundStart();
}


void CWeaponGravityGun::SoundDestroy( void )
{
	SoundStop();
}


void CWeaponGravityGun::SoundStop( void )
{
	CTFPlayer *pOwner = GetTFPlayerOwner();
	if ( !pOwner )
		return;

	switch( m_soundState )
	{
	case SS_SCANNING:
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
		break;
	case SS_LOCKEDON:
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLockedOn );
		m_sndLockedOn = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLightObject );
		m_sndLightObject = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndHeavyObject );
		m_sndHeavyObject = NULL;
		break;
	}
}



//-----------------------------------------------------------------------------
// Purpose: returns the linear fraction of value between low & high (0.0 - 1.0) * scale
//			e.g. UTIL_LineFraction( 1.5, 1, 2, 1 ); will return 0.5 since 1.5 is
//			halfway between 1 and 2
// Input  : value - a value between low & high (clamped)
//			low - the value that maps to zero
//			high - the value that maps to "scale"
//			scale - the output scale
// Output : parametric fraction between low & high
//-----------------------------------------------------------------------------
static float UTIL_LineFraction( float value, float low, float high, float scale )
{
	if ( value < low )
		value = low;
	if ( value > high )
		value = high;

	float delta = high - low;
	if ( delta == 0 )
		return 0;
	
	return scale * (value-low) / delta;
}

void CWeaponGravityGun::SoundStart( void )
{
	CPASAttenuationFilter filter( this );

	switch( m_soundState )
	{
	case SS_SCANNING:
		{
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.Scanning", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndMotor, 1.0f, 100 );
		}
		break;
	case SS_LOCKEDON:
		{
			m_sndLockedOn = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.LockedOn", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndLockedOn, 1.0f, 100 );
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.Scanning", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndMotor, 1.0f, 100 );
			m_sndLightObject = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.LightObject", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndLightObject, 1.0f, 100 );
			m_sndHeavyObject = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.HeavyObject", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndHeavyObject, 1.0f, 100 );
		}
		break;
	}
													//   volume, att, flags, pitch
}

void CWeaponGravityGun::SoundUpdate( void )
{
	int newState;
	
	if ( m_hObject )
		newState = SS_LOCKEDON;
	else
		newState = SS_SCANNING;

	if ( newState != m_soundState )
	{
		SoundStop();
		m_soundState = newState;
		SoundStart();
	}

	/*switch( m_soundState )
	{
	case SS_SCANNING:
		break;
	case SS_LOCKEDON:
		{
			CPASAttenuationFilter filter( this );

			float height = m_hObject->GetAbsOrigin().z - m_originalObjectPosition.z;

			// go from pitch 90 to 150 over a height of 500
			int pitch = 90 + (int)UTIL_LineFraction( height, 0, 500, 60 );

			assert(m_sndLockedOn!=NULL);
			if ( m_sndLockedOn != NULL )
			{
				(CSoundEnvelopeController::GetController()).SoundChangePitch( m_sndLockedOn, pitch, 0.0f );
			}

			// attenutate the movement sounds over 200 units of movement
			float distance = UTIL_LineFraction( m_movementLength, 0, 200, 1.0 );

			// blend the "mass" sounds between 50 and 500 kg
			IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
			if ( pPhys == NULL )
			{
				// we no longer exist!
				break;
			}
			
			float fade = UTIL_LineFraction( pPhys->GetMass(), 50, 500, 1.0 );

			(CSoundEnvelopeController::GetController()).SoundChangeVolume( m_sndLightObject, fade * distance, 0.0f );

			(CSoundEnvelopeController::GetController()).SoundChangeVolume( m_sndHeavyObject, (1.0 - fade) * distance, 0.0f );
		}
		break;
	}*/
}


CBaseEntity *CWeaponGravityGun::GetBeamEntity()
{
	CTFPlayer *pOwner = GetTFPlayerOwner();
	if ( !pOwner )
		return NULL;

	// Make sure I've got a view model
	CBaseViewModel *vm = pOwner->GetViewModel();
	if ( vm )
		return vm;

	return pOwner;
}

void CWeaponGravityGun::EffectDestroy( void )
{
#ifdef GAME_DLL
	lagcompensation->FinishLagCompensation( GetTFPlayerOwner() );
#endif
	m_active = false;
	SoundStop();

	DetachObject();

#ifdef GAME_DLL
	DestroyPhysgunDot();
	// delete portal or disaster will happen
	CProp_Portal *pPortal = (CProp_Portal*)gEntList.FindEntityByClassname( NULL, "prop_portal" );
	if ( pPortal )
	{
		pPortal->DoFizzleEffect( PORTAL_FIZZLE_KILLED, false );
		pPortal->Fizzle();
	}
#endif
}

void CWeaponGravityGun::UpdateObject( void )
{
	CTFPlayer *pTFPlayer = GetTFPlayerOwner();
	Assert( pTFPlayer );

	CBaseEntity *pObject = m_hObject;
	if ( !pObject )
		return;

	if ( !m_gravCallback.UpdateObject( pTFPlayer, pObject ) )
	{
		pTFPlayer->SetHeldObjectOnOppositeSideOfPortal( false );
		DetachObject();
		return;
	}
}

void CWeaponGravityGun::DetachObject( void )
{
	if ( m_hObject )
	{
		if ( m_hObject->VPhysicsGetObject() )
		{
#ifdef GAME_DLL
			CTFPlayer *pOwner = GetTFPlayerOwner();
			if( pOwner != NULL )
				pOwner->SetHeldObjectOnOppositeSideOfPortal( false );
#endif

			IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int count = m_hObject->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
			for ( int i = 0; i < count; i++ )
			{
				PhysClearGameFlags( pList[i], FVPHYSICS_PLAYER_HELD );
			}
			m_gravCallback.DetachEntity();
			m_hObject = NULL;
			m_physicsBone = 0;
		}
		else
		{
#ifdef GAME_DLL
			if ( m_hPhysgunDot )
				m_hObject->SetParent( NULL );
#endif
		}
	}
}

void CWeaponGravityGun::AttachObject( CBaseEntity *pObject, short physicsbone, const Vector& start, const Vector &end, float distance )
{
	CTFPlayer *pOwner = GetTFPlayerOwner();
	if( !pOwner )
		return;

	m_hObject = pObject;
	m_physicsBone = physicsbone;
	m_useDown = false;

	IPhysicsObject *pPhysics = GetPhysObjFromPhysicsBone( pObject, physicsbone );

	if ( pPhysics && pObject->VPhysicsGetObject() )
	{
		m_distance = distance;

		Vector worldPosition;
		pPhysics->WorldToLocal( &worldPosition, end );
		m_worldPosition = worldPosition;
		Vector vecOrigin;
		pPhysics->GetPosition( &vecOrigin, NULL );
		m_gravCallback.AttachEntity( pOwner, pObject, pPhysics, physicsbone, vecOrigin );

		m_originalObjectPosition = vecOrigin;

		pPhysics->Wake();
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = pObject->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			PhysSetGameFlags( pList[i], FVPHYSICS_PLAYER_HELD );
		}

#ifdef GAME_DLL
		Pickup_OnPhysGunPickup( pObject, pOwner );
#endif
	}
	else
	{
		m_distance = distance;

		m_worldPosition = pObject->WorldSpaceCenter();
		Vector vecOrigin = pObject->GetAbsOrigin();
		m_originalObjectPosition = vecOrigin;

		m_physicsBone = 0;
#ifdef GAME_DLL
		if ( m_hPhysgunDot )
			pObject->SetParent( m_hPhysgunDot );
#endif
	}
}

//=========================================================
//=========================================================
void CWeaponGravityGun::PrimaryAttack( void )
{
	if ( !CanAttack() )
		return;

	if ( !m_active )
	{
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		EffectCreate();
		SoundCreate();
	}
	else
	{
		EffectUpdate();
		SoundUpdate();
	}
}

void CWeaponGravityGun::SecondaryAttack( void )
{
	if ( !CanAttack() )
		return;

	EffectUpdate();
	SoundUpdate();
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: Third-person function call to render world model
//-----------------------------------------------------------------------------
int CWeaponGravityGun::DrawModel( int flags )
{
	// Only render these on the transparent pass
	if ( flags & STUDIO_TRANSPARENCY )
	{
		if ( !m_active )
			return 0;

		C_TFPlayer *pOwner = GetTFPlayerOwner();
		if ( !pOwner )
			return 0;

		Vector points[3];
		QAngle tmpAngle;

		C_BaseEntity *pObject = m_hObject;
		//if ( pObject == NULL )
		//	return 0;

		GetAttachment( 1, points[0], tmpAngle );

		// a little noise 11t & 13t should be somewhat non-periodic looking
		//points[1].z += 4*sin( gpGlobals->curtime*11 ) + 5*cos( gpGlobals->curtime*13 );
		if ( pObject == NULL )
		{
			//points[2] = m_targetPosition;
			trace_t tr;
			TraceLine( &tr );
			points[2] = tr.endpos;
		}
		else
		{
			pObject->EntityToWorldSpace( m_worldPosition, &points[2] );
		}

		Vector forward, right, up;
		QAngle playerAngles = pOwner->EyeAngles();
		AngleVectors( playerAngles, &forward, &right, &up );
		if ( pObject == NULL )
		{
			Vector vecDir = points[2] - points[0];
			VectorNormalize( vecDir );
			points[1] = points[0] + 0.5f * (vecDir * points[2].DistTo(points[0]));
		}
		else
		{
			Vector vecSrc = pOwner->Weapon_ShootPosition( );
			points[1] = vecSrc + 0.5f * (forward * points[2].DistTo(points[0]));
		}
		
		IMaterial *pMat = materials->FindMaterial( PHYSGUN_BEAM_SPRITE1, TEXTURE_GROUP_CLIENT_EFFECTS );
		if ( pObject )
			pMat = materials->FindMaterial( PHYSGUN_BEAM_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS );
		Vector color;
		color.Init(1,1,1);

		float scrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Bind( pMat );
		DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, scrollOffset );
		DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, -scrollOffset );

		IMaterial *pMaterial = materials->FindMaterial( PHYSGUN_BEAM_GLOW, TEXTURE_GROUP_CLIENT_EFFECTS );

		color32 clr={0,64,255,255};
		if ( pObject )
		{
			clr.r = 186;
			clr.g = 253;
			clr.b = 247;
			clr.a = 255;
		}

		float scale = random->RandomFloat( 3, 5 ) * ( pObject ? 3 : 2 );

		// Draw the sprite
		pRenderContext->Bind( pMaterial );
		for ( int i = 0; i < 3; i++ )
		{
			DrawSprite( points[2], scale, scale, clr );
		}
		return 1;
	}

	// Only do this on the opaque pass
	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Purpose: First-person function call after viewmodel has been drawn
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ViewModelDrawn( C_BaseViewModel *pBaseViewModel )
{
	if ( !m_active )
		return;

	// Render our effects
	C_BasePlayer *pOwner = GetTFPlayerOwner();

	if ( !pOwner )
		return;

	Vector points[3];
	QAngle tmpAngle;

	C_BaseEntity *pObject = m_hObject;
	//if ( pObject == NULL )
	//	return;

	pBaseViewModel->GetAttachment( 1, points[0], tmpAngle );

	// a little noise 11t & 13t should be somewhat non-periodic looking
	//points[1].z += 4*sin( gpGlobals->curtime*11 ) + 5*cos( gpGlobals->curtime*13 );
	if ( pObject == NULL )
	{
		//points[2] = m_targetPosition;
		trace_t tr;
		TraceLine( &tr );
		points[2] = tr.endpos;
	}
	else
	{
		pObject->EntityToWorldSpace(m_worldPosition, &points[2]);
	}

	Vector forward, right, up;
	QAngle playerAngles = pOwner->EyeAngles();
	AngleVectors( playerAngles, &forward, &right, &up );
	Vector vecSrc = pOwner->Weapon_ShootPosition( );
	points[1] = vecSrc + 0.5f * (forward * points[2].DistTo(points[0]));
	
	IMaterial *pMat = materials->FindMaterial( PHYSGUN_BEAM_SPRITE1, TEXTURE_GROUP_CLIENT_EFFECTS );
	if ( pObject )
		pMat = materials->FindMaterial( PHYSGUN_BEAM_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS );
	Vector color;
	color.Init(1,1,1);

	// Now draw it.
	CViewSetup beamView = *view->GetPlayerViewSetup();

	Frustum dummyFrustum;
	render->Push3DView( beamView, 0, NULL, dummyFrustum );

	float scrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMat );
#if 1
	// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
	// Force clipped down range
	pRenderContext->DepthRange( 0.1f, 0.2f );
#endif
	DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, scrollOffset );
	DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, -scrollOffset );

	IMaterial *pMaterial = materials->FindMaterial( PHYSGUN_BEAM_GLOW, TEXTURE_GROUP_CLIENT_EFFECTS );

	color32 clr={0,64,255,255};
	if ( pObject )
	{
		clr.r = 186;
		clr.g = 253;
		clr.b = 247;
		clr.a = 255;
	}

	float scale = random->RandomFloat( 3, 5 ) * ( pObject ? 3 : 2 );

	// Draw the sprite
	pRenderContext->Bind( pMaterial );
	for ( int i = 0; i < 3; i++ )
	{
		DrawSprite( points[2], scale, scale, clr );
	}
#if 1
	pRenderContext->DepthRange( 0.0f, 1.0f );
#endif

	render->PopView( dummyFrustum );

	// Pass this back up
	BaseClass::ViewModelDrawn( pBaseViewModel );
}

//-----------------------------------------------------------------------------
// Purpose: We are always considered transparent
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::IsTransparent( void )
{
	return false;
}
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ItemPreFrame()
{
	BaseClass::ItemPreFrame();

#ifdef GAME_DLL
	// Update the object if the weapon is switched on.
	if( m_active )
	{
		UpdateObject();
	}
#endif
}

void CWeaponGravityGun::ItemPostFrame( void )
{
	CTFPlayer *pOwner = GetTFPlayerOwner();
	if ( !pOwner )
		return;

	// adnan
	// this is where we check if we're orbiting the object
	// if we're holding something and pressing use,
	//  then set us in the orbiting state
	//  - this will indicate to OverrideMouseInput that we should zero the input and update our delta angles
	//  UPDATE: not anymore.  now this just sets our state variables.
	CBaseEntity *pObject = m_hObject;
	if( pObject ) 
	{
		if( ( pOwner->m_nButtons & IN_ATTACK ) && ( pOwner->m_nButtons & IN_USE ) ) 
		{
			m_gravCallback.m_bHasRotatedCarryAngles = true;
			
			// did we JUST hit use?
			//  if so, grab the current angles to begin with as the rotated angles
			if( !(pOwner->m_afButtonLast & IN_USE) ) 
			{
				m_gravCallback.m_vecRotatedCarryAngles = pObject->GetAbsAngles();
			}

			m_bIsCurrentlyRotating = true;
		}
		else 
		{
			m_gravCallback.m_bHasRotatedCarryAngles = false;

			m_bIsCurrentlyRotating = false;
		}
	} 
	else 
	{
		m_bIsCurrentlyRotating = false;

		m_gravCallback.m_bHasRotatedCarryAngles = false;
	}
	// end adnan

	if ( pOwner->m_nButtons & IN_ATTACK )
	{
#if defined( CLIENT_DLL )
		if( (pOwner->m_nButtons & IN_USE) ) {
			pOwner->m_vecUseAngles = pOwner->pl.v_angle;
		}
#endif //CLIENT_DLL
		if ( pOwner->m_afButtonPressed & IN_ATTACK2 )
		{
			SecondaryAttack();
		}
		else if ( pOwner->m_nButtons & IN_ATTACK2 )
		{
			if ( m_active )
			{
				EffectDestroy();
				SoundDestroy();
			}
			WeaponIdle();
			return;
		}
		PrimaryAttack();
	}
	else 
	{
		if ( m_active )
		{
			EffectDestroy();
			SoundDestroy();
		}
		WeaponIdle();
		return;
	}

	if( ( pOwner->m_nButtons & IN_ATTACK ) && ( pOwner->m_afButtonPressed & IN_RELOAD ) )
	{
		Reload();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::Deploy( void )
{
#ifdef GAME_DLL
	// delete portal or disaster will happen
	CProp_Portal *pPortal = (CProp_Portal*)gEntList.FindEntityByClassname( NULL, "prop_portal" );
	if ( pPortal )
	{
		pPortal->DoFizzleEffect( PORTAL_FIZZLE_KILLED, false );
		pPortal->Fizzle();
	}
#endif
	return BaseClass::Deploy();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::HasAnyAmmo( void )
{
	//Always report that we have ammo
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: the DEATH reload
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::Reload( void )
{
#ifdef GAME_DLL
	CBaseEntity *pObject = m_hObject;
	if ( pObject )
	{
		CTFPlayer *pOwner = GetTFPlayerOwner();
		if ( pOwner )
		{
			CTakeDamageInfo info( pOwner, pOwner, 1000.0, DMG_PHYSGUN, LFE_DMG_CUSTOM_PHYSCANNON_MEGA );
			pObject->TakeDamage( info );
			return true;
		}
	}
#endif
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: TF Slot Hack
//-----------------------------------------------------------------------------
int CWeaponGravityGun::GetSlot(void) const
{
	CTFPlayer *pOwner = GetTFPlayerOwner();

	if ( pOwner )
	{
		if ( pOwner->IsPlayerClass( TF_CLASS_ENGINEER ) )
			return 7;
		else if ( pOwner->IsPlayerClass( TF_CLASS_SPY ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_SCOUT ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_SOLDIER ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_DEMOMAN ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_PYRO ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_MEDIC ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_SNIPER ) )
			return 6;
		else if ( pOwner->IsPlayerClass( TF_CLASS_HEAVYWEAPONS ) )
			return 6;
	}
	
	return BaseClass::GetSlot();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponGravityGun::FallInit( void )
{
#ifndef CLIENT_DLL
	// Skip TF weapon base as it prevents FallInit of base weapon from running.
	CBaseCombatWeapon::FallInit();
#endif
}
//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponGravityGun::CreatePhysgunDot( void )
{
#ifdef GAME_DLL
	if ( m_hPhysgunDot )
		return;

	CTFPlayer *pPlayer = GetTFPlayerOwner();
	if ( !pPlayer )
		return;

	m_hPhysgunDot = CPhysgunDot::Create( GetAbsOrigin(), pPlayer, true );
	m_hPhysgunDot->ChangeTeam( pPlayer->GetTeamNumber() );
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DestroyPhysgunDot( void )
{
#ifdef GAME_DLL
	if ( m_hPhysgunDot )
	{
		UTIL_Remove( m_hPhysgunDot );
		m_hPhysgunDot = NULL;
	}
#endif
}

// -----------------------------------------------------------------------------
// Purpose:
// -----------------------------------------------------------------------------
int CWeaponGravityGun::GetActivityWeaponRole( void )
{
	CTFPlayer *pOwner = ToTFPlayer( GetOwner() );
	if ( pOwner )
	{
		if ( pOwner->IsPlayerClass( TF_CLASS_SCOUT ) || pOwner->IsPlayerClass( TF_CLASS_ENGINEER ) )
			return TF_WPN_TYPE_PRIMARY;
	}

	return TF_WPN_TYPE_SECONDARY;
}