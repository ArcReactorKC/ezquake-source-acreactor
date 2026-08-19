/* Optional client-side aiming support.  Kept separate from input and prediction. */
#include "quakedef.h"
#include "aimbot.h"
#include "pmove.h"
#include <float.h>

#define AIMBOT_ROCKET_SPEED 1000.0f
#define AIMBOT_PLAYER_BOTTOM -24.0f
#define AIMBOT_PLAYER_TOP 32.0f

cvar_t aimbot_enabled = { "aimbot_enabled", "0" };
cvar_t aimbot_fov = { "aimbot_fov", "30" };
cvar_t aimbot_smooth = { "aimbot_smooth", "0.25" };
cvar_t aimbot_target_mode = { "aimbot_target_mode", "0" };
cvar_t aimbot_visibility = { "aimbot_visibility", "1" };
cvar_t aimbot_teamcheck = { "aimbot_teamcheck", "1" };
cvar_t aimbot_lock = { "aimbot_lock", "1" };
cvar_t aimbot_height = { "aimbot_height", "0.5" };
cvar_t aimbot_prediction = { "aimbot_prediction", "1" };
cvar_t aimbot_prediction_scale = { "aimbot_prediction_scale", "1.0" };
cvar_t aimbot_debug = { "aimbot_debug", "0" };
cvar_t aimbot_marker = { "aimbot_marker", "0" };

static int aimbot_target = -1;
static qbool aimbot_button;
static float aimbot_distance;
static float aimbot_angle;
static float aimbot_lead_time;
static double aimbot_debug_time;

static float Aimbot_AngleDelta(float to, float from)
{
	float delta = to - from;
	while (delta > 180.0f) delta -= 360.0f;
	while (delta < -180.0f) delta += 360.0f;
	return delta;
}

static player_state_t *Aimbot_State(int playernum)
{
	return &cl.frames[cl.parsecount & UPDATE_MASK].playerstate[playernum];
}

static qbool Aimbot_TargetValid(int playernum)
{
	player_info_t *info;
	player_state_t *state;
	player_info_t *local;

	if (playernum < 0 || playernum >= MAX_CLIENTS || playernum == cl.playernum)
		return false;
	info = &cl.players[playernum];
	state = Aimbot_State(playernum);
	if (!info->name[0] || info->spectator || info->dead || state->messagenum != cl.parsecount ||
		!state->modelindex || state->modelindex != cl_modelindices[mi_player] || ISDEAD(state->frame))
		return false;
	local = &cl.players[cl.playernum];
	if (aimbot_teamcheck.integer && cl.teamplay && local->team[0] && info->team[0] &&
		!strcmp(local->team, info->team))
		return false;
	return true;
}

static void Aimbot_GetAimPosition(int playernum, vec3_t position)
{
	player_state_t *state = Aimbot_State(playernum);
	float height = bound(0.0f, aimbot_height.value, 1.0f);

	VectorCopy(state->client_origin, position);
	if (!(state->antilag_flags & dbg_antilag_client_present))
		VectorCopy(state->origin, position);
	position[2] += AIMBOT_PLAYER_BOTTOM + height * (AIMBOT_PLAYER_TOP - AIMBOT_PLAYER_BOTTOM);
}

static qbool Aimbot_Visible(vec3_t source, vec3_t target)
{
	trace_t trace;
	vec3_t remaining;

	if (!aimbot_visibility.integer)
		return true;
	trace = PM_TraceLine(source, target);
	VectorSubtract(target, trace.endpos, remaining);
	return trace.fraction == 1.0f || VectorLength(remaining) <= 32.0f;
}

/* Solve |relative + velocity*t| = projectile_speed*t and use the earliest
 * positive root.  Scaling motion (rather than time) keeps tuning predictable. */
static qbool Aimbot_Intercept(vec3_t source, vec3_t target, vec3_t velocity, vec3_t result, float *lead_time)
{
	vec3_t relative, scaled_velocity;
	float pred_scale = bound(0.0f, aimbot_prediction_scale.value, 2.0f);
	float a, b, c, discriminant, t;

	VectorSubtract(target, source, relative);
	VectorScale(velocity, pred_scale, scaled_velocity);
	a = DotProduct(scaled_velocity, scaled_velocity) - AIMBOT_ROCKET_SPEED * AIMBOT_ROCKET_SPEED;
	b = 2.0f * DotProduct(relative, scaled_velocity);
	c = DotProduct(relative, relative);
	if (fabs(a) < 0.0001f) {
		if (fabs(b) < 0.0001f)
			return false;
		t = -c / b;
	}
	else {
		discriminant = b * b - 4.0f * a * c;
		if (discriminant < 0.0f)
			return false;
		t = (-b - sqrt(discriminant)) / (2.0f * a);
		if (t <= 0.0f)
			t = (-b + sqrt(discriminant)) / (2.0f * a);
	}
	if (t <= 0.0f || !isfinite(t))
		return false;
	VectorMA(target, t, scaled_velocity, result);
	*lead_time = t;
	return true;
}

static qbool Aimbot_Evaluate(int playernum, vec3_t source, float fov, float *angle, float *distance, vec3_t aim)
{
	vec3_t direction, angles;

	if (!Aimbot_TargetValid(playernum))
		return false;
	Aimbot_GetAimPosition(playernum, aim);
	VectorSubtract(aim, source, direction);
	*distance = VectorLength(direction);
	if (*distance < 0.001f)
		return false;
	vectoangles(direction, angles);
	angles[PITCH] = -angles[PITCH];
	*angle = sqrt(Aimbot_AngleDelta(angles[PITCH], cl.viewangles[PITCH]) *
		Aimbot_AngleDelta(angles[PITCH], cl.viewangles[PITCH]) +
		Aimbot_AngleDelta(angles[YAW], cl.viewangles[YAW]) *
		Aimbot_AngleDelta(angles[YAW], cl.viewangles[YAW]));
	return *angle <= fov && Aimbot_Visible(source, aim);
}

static int Aimbot_FindTarget(vec3_t source, float fov)
{
	int i, best = -1;
	float angle, distance, score, best_score = FLT_MAX;
	vec3_t aim;

	for (i = 0; i < MAX_CLIENTS; ++i) {
		if (!Aimbot_Evaluate(i, source, fov, &angle, &distance, aim))
			continue;
		score = aimbot_target_mode.integer == 1 ? distance : angle;
		if (score < best_score) {
			best_score = score;
			best = i;
		}
	}
	return best;
}

void Aimbot_Reset(void)
{
	aimbot_target = -1;
	aimbot_distance = aimbot_angle = aimbot_lead_time = 0.0f;
}

static void Aimbot_Toggle_f(void)
{
	Cvar_SetValue(&aimbot_enabled, !aimbot_enabled.integer);
	if (!aimbot_enabled.integer)
		Aimbot_Reset();
	Com_Printf("Aimbot %s\n", aimbot_enabled.integer ? "enabled" : "disabled");
}

static void Aimbot_Status_f(void)
{
	Com_Printf("Aimbot: %s\nFOV: %.0f\nSmooth: %.2f\nVisibility: %s\nTeam check: %s\n"
		"Target lock: %s\nPrediction: %s\nTarget mode: %s\nCurrent target: %s\n",
		aimbot_enabled.integer ? "enabled" : "disabled", bound(1.0f, aimbot_fov.value, 180.0f),
		bound(0.0f, aimbot_smooth.value, 1.0f), aimbot_visibility.integer ? "enabled" : "disabled",
		aimbot_teamcheck.integer ? "enabled" : "disabled", aimbot_lock.integer ? "enabled" : "disabled",
		aimbot_prediction.integer ? "enabled" : "disabled",
		aimbot_target_mode.integer == 1 ? "closest-by-distance" : "closest-to-crosshair",
		aimbot_target >= 0 && Aimbot_TargetValid(aimbot_target) ? cl.players[aimbot_target].name : "none");
}

static void Aimbot_Down_f(void) { aimbot_button = true; }
static void Aimbot_Up_f(void) { aimbot_button = false; if (!aimbot_enabled.integer) Aimbot_Reset(); }

void Aimbot_Frame(void)
{
	vec3_t source, aim, direction, desired;
	float fov = bound(1.0f, aimbot_fov.value, 180.0f);
	float smooth, fraction;
	player_state_t *state;

	if ((!aimbot_enabled.integer && !aimbot_button) || cls.state != ca_active || cls.demoplayback ||
		cl.spectator || cl.intermission || cl.stats[STAT_HEALTH] <= 0) {
		Aimbot_Reset();
		return;
	}
	VectorCopy(cl.simorg, source);
	source[2] += (cl.z_ext & Z_EXT_VIEWHEIGHT) ? cl.stats[STAT_VIEWHEIGHT] : DEFAULT_VIEWHEIGHT;
	if (!aimbot_lock.integer || !Aimbot_Evaluate(aimbot_target, source, fov * 1.25f,
		&aimbot_angle, &aimbot_distance, aim))
		aimbot_target = Aimbot_FindTarget(source, fov);
	if (!Aimbot_Evaluate(aimbot_target, source, fov * (aimbot_lock.integer ? 1.25f : 1.0f),
		&aimbot_angle, &aimbot_distance, aim)) {
		Aimbot_Reset();
		return;
	}
	aimbot_lead_time = 0.0f;
	state = Aimbot_State(aimbot_target);
	if (aimbot_prediction.integer && cl.stats[STAT_ACTIVEWEAPON] == IT_ROCKET_LAUNCHER)
		Aimbot_Intercept(source, aim, state->velocity, aim, &aimbot_lead_time);
	VectorSubtract(aim, source, direction);
	if (VectorLength(direction) < 0.001f)
		return;
	vectoangles(direction, desired);
	desired[PITCH] = -desired[PITCH];
	smooth = bound(0.0f, aimbot_smooth.value, 1.0f);
	/* Exponential response is stable across frame rates; 0 remains an instant snap. */
	fraction = smooth <= 0.0f ? 1.0f : 1.0f - expf(-(12.0f * (1.0f - smooth) + 0.5f) * cls.trueframetime);
	cl.viewangles[PITCH] += Aimbot_AngleDelta(desired[PITCH], cl.viewangles[PITCH]) * fraction;
	cl.viewangles[YAW] += Aimbot_AngleDelta(desired[YAW], cl.viewangles[YAW]) * fraction;
	cl.viewangles[PITCH] = bound(cl.minpitch, cl.viewangles[PITCH], cl.maxpitch);
	cl.viewangles[YAW] = anglemod(cl.viewangles[YAW]);
	if (aimbot_debug.integer && cls.realtime >= aimbot_debug_time) {
		aimbot_debug_time = cls.realtime + 1.0;
		Com_Printf("AIMBOT Target: %s Distance: %.0f Angle: %.1f Visible: yes Weapon: %s Prediction: %s Lead: %.3fs\n",
			cl.players[aimbot_target].name, aimbot_distance, aimbot_angle,
			cl.stats[STAT_ACTIVEWEAPON] == IT_ROCKET_LAUNCHER ? "rocket launcher" : "hitscan/other",
			aimbot_lead_time > 0 ? "on" : "off", aimbot_lead_time);
	}
}

void Aimbot_Init(void)
{
	Cvar_SetCurrentGroup(CVAR_GROUP_INPUT_MISC);
	Cvar_Register(&aimbot_enabled); Cvar_Register(&aimbot_fov); Cvar_Register(&aimbot_smooth);
	Cvar_Register(&aimbot_target_mode); Cvar_Register(&aimbot_visibility); Cvar_Register(&aimbot_teamcheck);
	Cvar_Register(&aimbot_lock); Cvar_Register(&aimbot_height); Cvar_Register(&aimbot_prediction);
	Cvar_Register(&aimbot_prediction_scale); Cvar_Register(&aimbot_debug); Cvar_Register(&aimbot_marker);
	Cvar_ResetCurrentGroup();
	Cmd_AddCommand("aimbot_toggle", Aimbot_Toggle_f); Cmd_AddCommand("aimbot_status", Aimbot_Status_f);
	Cmd_AddCommand("+aimbot", Aimbot_Down_f); Cmd_AddCommand("-aimbot", Aimbot_Up_f);
	Aimbot_Reset();
}
