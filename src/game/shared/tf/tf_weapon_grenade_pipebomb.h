//====== Copyright ? 1996-2020, Valve Corporation, All rights reserved. ========//
//
// Purpose: TF Pipebomb Grenade.
//
//=============================================================================//
#ifndef TF_WEAPON_GRENADE_PIPEBOMB_H
#define TF_WEAPON_GRENADE_PIPEBOMB_H
#ifdef _WIN32
#pragma once
#endif

#include "tf_weaponbase_grenadeproj.h"

#ifdef GAME_DLL
#include "vphysics/constraints.h"
#include "physics_saverestore.h"
#endif

// Client specific.
#ifdef CLIENT_DLL
#define CTFGrenadePipebombProjectile C_TFGrenadePipebombProjectile
#endif

//=============================================================================
//
// TF Pipebomb Grenade
//
class CTFGrenadePipebombProjectile : public CTFWeaponBaseGrenadeProj
{
public:

	DECLARE_CLASS( CTFGrenadePipebombProjectile, CTFWeaponBaseGrenadeProj );
	DECLARE_NETWORKCLASS();

	CTFGrenadePipebombProjectile();
	~CTFGrenadePipebombProjectile();

	// Unique identifier.
	virtual int			GetWeaponID( void ) const;

	int					GetType( void )							{ return m_iType; } 
	virtual int			GetDamageType();

	void				SetChargeTime( float flChargeTime )		{ m_flChargeTime = flChargeTime; }

	CNetworkVar( int, m_iType ); // TF_GL_MODE_REGULAR, TF_GL_MODE_REMOTE_DETONATE, TF_GL_MODE_FIZZLE
	CNetworkVar( bool, m_bDefensiveBomb );
	float		m_flCreationTime;
	float		m_flChargeTime;
	bool		m_bPulsed;
	float		m_flFullDamage;
	float		m_flDamageMult;
	
	virtual void	UpdateOnRemove( void );

	virtual float	GetLiveTime( void ) const;

#ifdef CLIENT_DLL

	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual const char *GetTrailParticleName( void );
	virtual void	CreateTrails( void );
	virtual int		DrawModel( int flags );
	virtual void	Simulate( void );

	virtual void	CreateLightEffects( void );

	CGlowObject *m_pGlowObject;
	bool		m_bGlowing;

#else

	DECLARE_DATADESC();

	// Creation.
	static CTFGrenadePipebombProjectile *Create( const Vector &position, const QAngle &angles, const Vector &velocity, const AngularImpulse &angVelocity,
												 CBaseCombatCharacter *pOwner, int iMode, float flDamageMult, 
												 CBaseEntity *pWeapon );

	// Overrides.
	virtual void	Spawn();
	virtual void	Precache();
	
	virtual void	BounceSound( void );
	virtual void	Detonate();
	virtual void	Fizzle();

	virtual void	DetonateStickies( void );

	void			SetPipebombMode( int iMode );
	void			SetPipebombBetaVariant( int iVariant );

	virtual void	PipebombTouch( CBaseEntity *pOther );
	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	virtual int		OnTakeDamage( const CTakeDamageInfo &info );

	virtual CBaseEntity		*GetEnemy( void )			{ return m_hEnemy; }
	
	virtual void	Deflected( CBaseEntity *pDeflectedBy, Vector &vecDir );

	bool			CreateConstraintToObject( CBaseEntity *pObject );
	void			DestroyConstraint( void );

	virtual void	OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason );
private:

	bool			m_bFizzle;
	float			m_flMinSleepTime;

	CHandle<CBaseEntity>	m_hEnemy;

	IPhysicsConstraint		*m_pConstraint;
	EHANDLE					m_hConstrainedEntity;
#endif
};
#endif // TF_WEAPON_GRENADE_PIPEBOMB_H