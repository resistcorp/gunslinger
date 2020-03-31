#include <gs.h>

// Globals
_global gs_resource( gs_vertex_buffer ) 	g_vbo = {0};
_global gs_resource( gs_index_buffer ) 		g_ibo = {0};
_global gs_resource( gs_command_buffer ) 	g_cb = {0};
_global gs_resource( gs_shader ) 			g_shader = {0};
_global gs_resource( gs_uniform ) 			u_tex = {0}; 
_global gs_resource( gs_texture ) 			g_tex = {0};

#if (defined GS_PLATFORM_APPLE)
	_global const s32 g_window_width 	= 800;
	_global const s32 g_window_height 	= 600;
#else
	_global const s32 g_window_width 	= 1258;
	_global const s32 g_window_height 	= 848;
#endif

_global const s32 g_texture_width 	= 1258 / 2;
_global const s32 g_texture_height 	= 848 / 2;

typedef struct color_t
{
	u8 r;
	u8 g;
	u8 b;
	u8 a;
} color_t;

typedef struct particle_t
{
	u8 id;
	f32 life_time;
	gs_vec2 velocity;
	color_t color;
	b32 has_been_updated_this_frame;
} particle_t;

// For now, all particle information will simply be a value to determine its material id
#define mat_id_empty (u8)0
#define mat_id_sand  (u8)1
#define mat_id_water (u8)2
#define mat_id_salt (u8)3
#define mat_id_brick (u8)4
#define mat_id_fire (u8)5
#define mat_id_smoke (u8)6
#define mat_id_ember (u8)7

// Colors
#define mat_col_empty (color_t){ 0, 0, 0, 0}
#define mat_col_sand  (color_t){ 150, 100, 50, 255 }
#define mat_col_salt  (color_t){ 200, 180, 190, 255 }
#define mat_col_water (color_t){ 20, 100, 170, 200 }
#define mat_col_stone (color_t){ 120, 110, 120, 255 }
#define mat_col_brick (color_t){ 60, 40, 20, 255 }
#define mat_col_fire  (color_t){ 150, 20, 0, 255 }
#define mat_col_smoke (color_t){ 30, 20, 15, 255 }
#define mat_col_ember (color_t){ 200, 120, 20, 255 }

typedef enum material_selection
{
	mat_sel_sand = 0x00,
	mat_sel_water,
	mat_sel_salt,
	mat_sel_brick,
	mat_sel_fire,
	mat_sel_smoke
} material_selection;

// Material selection for "painting" / default to sand
_global material_selection g_material_selection = mat_sel_sand;

// World update processing structure
_global u8* g_world_process_update_structure = {0};	// Every particle has id associated with it? Jeezuz...

// World particle data structure
_global particle_t* g_world_particle_data = {0};

// Texture buffers (double buffered color buffers)
_global color_t* g_texture_buffer = {0};

// Frame counter
_global u32 g_frame_counter = 0;

// World physics settings
_global f32 gravity = 10.f;

// Handle for main window
_global gs_resource_handle g_window;

const char* v_src = "\n"
"#version 330 core\n"
"layout (location = 0) in vec2 a_pos;\n"
"layout (location = 1) in vec2 a_texCoord;\n"
"out vec2 texCoord;\n"
"void main()\n"
"{\n"
"	gl_Position = vec4(a_pos, 0.0, 1.0);\n"
"	texCoord = vec2(a_texCoord.x, 1.0 - a_texCoord.y);\n"
"}";

const char* f_src = "\n"
"#version 330 core\n"
"out vec4 frag_color;\n"
"in vec2 texCoord;\n"
"uniform sampler2D u_tex;\n"
"void main()\n"
"{\n"
"	frag_color = texture(u_tex, texCoord);\n"
"}";

// Forward Decls.
gs_result app_init();		// Use to init your application
gs_result app_update();		// Use to update your application
gs_result app_shutdown();	// Use to shutdown your appliaction

particle_t particle_empty();
particle_t particle_sand();
particle_t particle_water();
particle_t particle_salt();
particle_t particle_brick();
particle_t particle_fire();
particle_t particle_smoke();
particle_t particle_ember();

void update_input();
void update_particle_sim();
void update_sand( u32 w, u32 h );
void update_water( u32 w, u32 h );
void update_salt( u32 w, u32 h );
void update_fire( u32 w, u32 h );
void update_smoke( u32 w, u32 h );
void update_ember( u32 w, u32 h );
void update_default( u32 w, u32 h );
void write_data( u32 idx, particle_t );
void render_scene();

gs_vec2 calculate_mouse_position( gs_vec2 pmp )
{
	gs_vec2 ws = gs_engine_instance()->ctx.platform->window_size( g_window );
	// Need to place mouse into frame
	f32 x_scale = pmp.x / (f32)ws.x;
	f32 y_scale = pmp.y / (f32)ws.y;
	return (gs_vec2){ x_scale * (f32)g_texture_width, y_scale * (f32)g_texture_height };
}

s32 random_val( s32 lower, s32 upper )
{
	if ( upper < lower ) {
		s32 tmp = lower;
		lower = upper;
		upper = tmp;
	}
	return ( rand() % (upper - lower + 1) + lower );
}

s32 compute_idx( s32 x, s32 y )
{
	return ( y * g_texture_width + x );
}

b32 in_bounds( s32 x, s32 y )
{
	if ( x < 0 || x > (g_texture_width - 1) || y < 0 || y > (g_texture_height - 1) ) return false;
	return true;
}

b32 is_empty( s32 x, s32 y )
{
	return ( in_bounds( x, y ) && g_world_particle_data[ compute_idx( x, y ) ].id == mat_id_empty );
}

particle_t get_particle_at( s32 x, s32 y )
{
	return g_world_particle_data[ compute_idx( x, y ) ];
}

b32 is_in_liquid( s32 x, s32 y ) 
{
	if ( in_bounds( x, y ) && get_particle_at( x, y ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x, y - 1 ) && get_particle_at( x, y - 1 ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x, y + 1 ) && get_particle_at( x, y + 1 ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x - 1, y ) && get_particle_at( x - 1, y ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x - 1, y - 1 ) && get_particle_at( x - 1, y - 1 ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x - 1, y + 1 ) && get_particle_at( x - 1, y + 1 ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x + 1, y ) && get_particle_at( x + 1, y ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x + 1, y - 1 ) && get_particle_at( x + 1, y - 1 ).id == mat_id_water ) {
		return true;
	}
	if ( in_bounds( x + 1, y + 1 ) && get_particle_at( x + 1, y + 1 ).id == mat_id_water ) {
		return true;
	}
	return false;
}

int main( int argc, char** argv )
{
	gs_application_desc app = {0};
	app.window_title 		= "SandSim";
	app.window_width 		= g_window_width;
	app.window_height 		= g_window_height;
	app.init 				= &app_init;
	app.update 				= &app_update;
	app.shutdown 			= &app_shutdown;
	app.frame_rate 			= 60;
	app.enable_vsync 		= false;

	// Construct internal instance of our engine
	gs_engine* engine = gs_engine_construct( app );

	// Run the internal engine loop until completion
	gs_result res = engine->run();

	gs_println("here");

	// Check result of engine after exiting loop
	if ( res != gs_result_success ) 
	{
		gs_println( "Error: Engine did not successfully finish running." );
		return -1;
	}

	gs_println( "Gunslinger exited successfully." );

	return 0;	
}

// Here, we'll initialize all of our application data, which in this case is our graphics resources
gs_result app_init()
{
	// Cache instance of graphics api from engine
	gs_graphics_i* gfx = gs_engine_instance()->ctx.graphics;

	// Construct command buffer ( the command buffer is used to allow for immediate drawing at any point in our program )
	g_cb = gfx->construct_command_buffer();

	// Construct shader from our source above
	g_shader = gfx->construct_shader( v_src, f_src );

	// Construct uniform for shader
	u_tex = gfx->construct_uniform( g_shader, "u_tex", gs_uniform_type_sampler2d );

	// Vertex data layout for our mesh (for this shader, it's a single float3 attribute for position)
	gs_vertex_attribute_type layout[] = 
	{
		gs_vertex_attribute_float2,
		gs_vertex_attribute_float2
	};
	// Count of our vertex attribute array
	u32 layout_count = sizeof( layout ) / sizeof( gs_vertex_attribute_type ); 

	// Vertex data for triangle
	f32 v_data[] = 
	{
		// Positions  UVs
		-1.0f, -1.0f,  0.0f, 0.0f,	// Top Left
		 1.0f, -1.0f,  1.0f, 0.0f,	// Top Right 
		-1.0f,  1.0f,  0.0f, 1.0f,  // Bottom Left
		 1.0f,  1.0f,  1.0f, 1.0f   // Bottom Right
	};

	u32 i_data[] = 
	{
		0, 2, 3,
		3, 1, 0
	};

	// Construct vertex buffer
	g_vbo = gfx->construct_vertex_buffer( layout, layout_count, v_data, sizeof(v_data) );
	// Construct index buffer
	g_ibo = gfx->construct_index_buffer( i_data, sizeof(i_data) );

	// Construct world data ( for now, it'll just be the size of the screen )
	g_world_particle_data = gs_malloc( g_texture_width * g_texture_height * sizeof(particle_t) );

	// Construct texture buffer data
	g_texture_buffer = gs_malloc( g_texture_width * g_texture_height * sizeof(color_t) );

	// Set buffers to 0
	memset( g_texture_buffer, 0, g_texture_width * g_texture_height * sizeof(color_t) );
	memset( g_world_particle_data, 0, g_texture_width * g_texture_height );

	// Construct texture resource from GPU
	gs_texture_parameter_desc t_desc = gs_texture_parameter_desc_default();
	t_desc.mag_filter = gs_nearest;
	t_desc.min_filter = gs_nearest;
	t_desc.generate_mips = false;
	t_desc.width = g_texture_width;
	t_desc.height = g_texture_height;
	t_desc.num_comps = 4;
	t_desc.data = g_texture_buffer;

	g_tex = gfx->construct_texture( t_desc );

	// Cache window handle from platform
	g_window = gs_engine_instance()->ctx.platform->main_window();

	return gs_result_success;
}

gs_result app_update()
{
	// Grab global instance of engine
	gs_engine* engine = gs_engine_instance();

	// If we press the escape key, exit the application
	if ( engine->ctx.platform->key_pressed( gs_keycode_esc ) )
	{
		return gs_result_success;
	}

	gs_timed_action( 10, 
	{
		gs_println( "frame: %.5f ms", engine->ctx.platform->time.frame );
	});

	// Handle inputs for painting
	update_input();

	// Update particle sim
	update_particle_sim();

	/*===============
	// Render scene
	================*/
	render_scene();

	return gs_result_in_progress;
}

gs_result app_shutdown()
{
	gs_println( "Goodbye, Gunslinger." );
	return gs_result_success;
}

void update_input()
{
	gs_platform_i* platform = gs_engine_instance()->ctx.platform;

	if ( platform->key_pressed( gs_keycode_one ) ) {
		g_material_selection = mat_sel_sand;
	}
	if ( platform->key_pressed( gs_keycode_two ) ) {
		g_material_selection = mat_sel_water;
	}
	if ( platform->key_pressed( gs_keycode_three ) ) {
		g_material_selection = mat_sel_salt;
	}
	if ( platform->key_pressed( gs_keycode_four ) ) {
		g_material_selection = mat_sel_brick;
	}
	if ( platform->key_pressed( gs_keycode_five ) ) {
		g_material_selection = mat_sel_fire;
	}
	if ( platform->key_pressed( gs_keycode_six ) ) {
		g_material_selection = mat_sel_smoke;
	}
}

void update_particle_sim()
{
	// Cache engine subsystem interfaces
	gs_graphics_i* gfx = gs_engine_instance()->ctx.graphics;
	gs_platform_i* platform = gs_engine_instance()->ctx.platform;

	// Update frame counter ( loop back to 0 if we roll past u32 max )
	b32 frame_counter_even = ((g_frame_counter % 2) == 0);
	s32 ran = frame_counter_even ? 0 : 1;

	const f32 dt = platform->time.delta;

	// Rip through read data and update write buffer
	// Note(John): We update "bottom up", since all the data is edited "in place". Double buffering all data would fix this 
	// 	issue, however it requires double all of the data.
	for ( u32 y = g_texture_height - 1; y > 0; --y )
	{
		for ( u32 x = ran ? 0 : g_texture_width - 1; ran ? x < g_texture_width : x > 0; ran ? ++x : --x )
		{
			// Current particle idx
			u32 read_idx = compute_idx( x, y );

			// Get material of particle at point
			u8 mat_id = get_particle_at( x, y ).id;

			// Update particle's lifetime (I guess just use frames)? Or should I have sublife?
			g_world_particle_data[ read_idx ].life_time += 1.f * dt;

			switch ( mat_id ) {

				case mat_id_sand:  update_sand( x, y );  break;
				case mat_id_water: update_water( x, y ); break;
				case mat_id_salt:  update_salt( x, y );  break;
				case mat_id_fire:  update_fire( x, y );  break;
				case mat_id_smoke: update_smoke( x, y ); break;
				case mat_id_ember: update_ember( x, y ); break;

				// Do nothing for empty or default case
				default:
				case mat_id_empty: 
				{
					// update_default( w, h, i ); 
				} break;
			}
		}
	}

	// Can remove this loop later on by keeping update structure and setting that for the particle as it moves, 
	// then at the end of frame just memsetting the entire structure to 0.
	for ( u32 y = g_texture_height - 1; y > 0; --y ) {
		for ( u32 x = ran ? 0 : g_texture_width - 1; ran ? x < g_texture_width : x > 0; ran ? ++x : --x ) {
			// Set particle's update to false for next frame
			g_world_particle_data[ compute_idx( x, y ) ].has_been_updated_this_frame = false;
		}
	}

	// Mouse input for testing
	if ( platform->mouse_down( gs_mouse_lbutton ) )
	{
		gs_vec2 mp = calculate_mouse_position( platform->mouse_position() );
		f32 mp_x = gs_clamp( mp.x, 0.f, (f32)g_texture_width - 1.f );	
		f32 mp_y = gs_clamp( mp.y, 0.f, (f32)g_texture_height - 1.f );
		u32 max_idx = (g_texture_width * g_texture_height) - 1;
		s32 r_amt = random_val( 1, 10000 );
		const f32 R = 10.f;

		// Spawn in a circle around the mouse
		for ( u32 i = 0; i < r_amt; ++i )
		{
			f32 ran = (f32)random_val(0, 100) / 100.f;
			f32 r = R * sqrt(ran);
			f32 theta = (f32)random_val(0, 100)/100.f * 2.f * gs_pi;
			f32 rx = cos((f32)theta) * r;
			f32 ry = sin((f32)theta) * r;
			s32 mpx = (s32)gs_clamp( mp_x + (f32)rx, 0.f, (f32)g_texture_width - 1.f );
			s32 mpy = (s32)gs_clamp( mp_y + (f32)ry, 0.f, (f32)g_texture_height - 1.f );
			s32 idx = mpy * (s32)g_texture_width + mpx;
			idx = gs_clamp( idx, 0, max_idx );

			if ( is_empty( mpx, mpy ) )
			{
				particle_t p = {0};
				switch ( g_material_selection ) {
					case mat_sel_sand: p = particle_sand(); break;;
					case mat_sel_water: p = particle_water(); break;;
					case mat_sel_salt: p = particle_salt(); break;;
					case mat_sel_brick: p = particle_brick(); break;;
					case mat_sel_fire: p = particle_fire(); break;
					case mat_sel_smoke: p = particle_smoke(); break;
				}
				p.velocity = (gs_vec2){ random_val( -1, 1 ), random_val( -2, 5 ) };
				write_data( idx, p );
			}
		}
	}

	// Solid Erase
	if (  platform->mouse_down( gs_mouse_rbutton ) )
	{
		gs_vec2 mp = calculate_mouse_position( platform->mouse_position() );
		f32 mp_x = gs_clamp( mp.x, 0.f, (f32)g_texture_width - 1.f );	
		f32 mp_y = gs_clamp( mp.y, 0.f, (f32)g_texture_height - 1.f );
		u32 max_idx = (g_texture_width * g_texture_height) - 1;
		const f32 R = 20.f;

		// Erase in a circle pattern
		for ( s32 i = -20 ; i < 20; ++i )
		{
			for ( s32 j = -20 ; j < 20; ++j )
			{
				s32 rx = ((s32)mp_x + j); 
				s32 ry = ((s32)mp_y + i);
				gs_vec2 r = (gs_vec2){ rx, ry };

				if ( in_bounds( rx, ry ) && gs_vec2_distance( mp, r ) <= R )
				{
					write_data( compute_idx( rx, ry ), particle_empty() );
				}
			}
		}
	}

	// Upload our updated texture data to GPU
	gs_texture_parameter_desc t_desc = gs_texture_parameter_desc_default();
	t_desc.mag_filter = gs_nearest;
	t_desc.min_filter = gs_nearest;
	t_desc.generate_mips = false;
	t_desc.width = g_texture_width;
	t_desc.height = g_texture_height;
	t_desc.num_comps = 4;
	t_desc.data = g_texture_buffer;
	gfx->update_texture_data( g_tex, t_desc );

	// Update frame counter
	g_frame_counter = (g_frame_counter + 1) % u32_max;
}

void render_scene()
{
	// Graphics api instance
	gs_graphics_i* gfx = gs_engine_instance()->ctx.graphics;

	// Platform api instance
	gs_platform_i* platform = gs_engine_instance()->ctx.platform;

	// Set clear color and clear screen
	f32 clear_color[4] = { 0.2f, 0.2f, 0.2f, 1.f };
	gfx->set_view_clear( g_cb, clear_color );

	gs_vec2 ws = platform->window_size( g_window );

	// This is to handle mac's retina high dpi for now until I fix that internally.
#if (defined GS_PLATFORM_APPLE)
	gfx->set_viewport( g_cb, (s32)ws.x * 2, (s32)ws.y * 2 );
#else
	gfx->set_viewport( g_cb, (s32)ws.x, (s32)ws.y );
#endif

	// Bind shader
	gfx->bind_shader( g_cb, g_shader );

	// Bind vertex buffer
	gfx->bind_vertex_buffer( g_cb, g_vbo );

	// Bind index buffer
	gfx->bind_index_buffer( g_cb, g_ibo );

	// Bind texture
	gfx->bind_texture( g_cb, u_tex, g_tex, 0 );

	// Draw
	gfx->draw_indexed( g_cb, 6 );

	// Submit command buffer for rendering
	gfx->submit_command_buffer( g_cb );
}

void write_data( u32 idx, particle_t p )
{
	// Write into particle data for id value
	g_world_particle_data[ idx ] = p;
	g_texture_buffer[ idx ] = p.color;

	// switch ( p.id )
	// {
	// 	case mat_id_sand:  g_texture_buffer[ idx ] = mat_col_sand;    break;
	// 	case mat_id_water: g_texture_buffer[ idx ] = mat_col_water;   break;
	// 	case mat_id_salt: g_texture_buffer[ idx ] = mat_col_salt;   break;
	// 	case mat_id_brick: g_texture_buffer[ idx ] = mat_col_brick;   break;
	// 	case mat_id_fire: g_texture_buffer[ idx ] = mat_col_fire;   break;
	// 	default:
	// 	case mat_id_empty: {
	// 		g_texture_buffer[ idx ] = mat_col_empty;
	// 	} break;
	// }
}

void update_salt( u32 x, u32 y )
{
	f32 dt = gs_engine_instance()->ctx.platform->time.delta;

	// For water, same as sand, but we'll check immediate left and right as well
	u32 read_idx = compute_idx( x, y );
	particle_t* p = &g_world_particle_data[ read_idx ];
	u32 write_idx = read_idx;
	u32 fall_rate = 4;

	p->velocity.y = gs_clamp( p->velocity.y + (gravity * dt), -10.f, 10.f );
	p->velocity.x = gs_clamp( p->velocity.x, -5.f, 5.f );

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if ( in_bounds( x, y + 1 ) && !is_empty( x, y + 1 ) && get_particle_at( x, y + 1 ).id != mat_id_water ) {
		p->velocity.y /= 2.f;
	}

	s32 vi_x = x + (s32)p->velocity.x; 
	s32 vi_y = y + (s32)p->velocity.y;

	// Check to see if you can swap first with other element below you
	u32 b_idx = compute_idx( x, y + 1 );
	u32 br_idx = compute_idx( x + 1, y + 1 );
	u32 bl_idx = compute_idx( x - 1, y + 1 );

	particle_t tmp_a = g_world_particle_data[ read_idx ];

	// Physics (using velocity)
	if ( in_bounds( vi_x, vi_y ) && (( is_empty( vi_x, vi_y ) ||
			((( g_world_particle_data[ compute_idx( vi_x, vi_y ) ].id == mat_id_water ) && 
			  !g_world_particle_data[ compute_idx( vi_x, vi_y ) ].has_been_updated_this_frame && 
			   gs_vec2_len(g_world_particle_data[compute_idx(vi_x, vi_y)].velocity) - gs_vec2_len(tmp_a.velocity) > 10.f ) ) ) ) ) {

		particle_t tmp_b = g_world_particle_data[ compute_idx( vi_x, vi_y ) ];

		// Try to throw water out
		if ( tmp_b.id == mat_id_water ) {

			tmp_b.has_been_updated_this_frame = true;

			s32 rx = random_val( -2, 2 );
			tmp_b.velocity = (gs_vec2){ rx, -3.f };

			write_data( compute_idx( vi_x, vi_y ), tmp_a );	

			for( s32 i = -10; i < 0; ++i ) {
				for ( s32 j = -5; j < 5; ++j ) {
					if ( is_empty( vi_x + j, vi_y + i ) ) {
						write_data( compute_idx( vi_x + j, vi_y + i ), tmp_b );
						break;
					}	
				}
			}

			// Couldn't write there, so, uh, destroy it.
			write_data( read_idx, particle_empty() );
		}
		else if ( is_empty( vi_x, vi_y ) ) {
			write_data( compute_idx( vi_x, vi_y ), tmp_a );
			write_data( read_idx, tmp_b );
		}
	}
	else if ( in_bounds( vi_x, vi_y ) && (( is_empty( vi_x, vi_y ) ||
			((( g_world_particle_data[ compute_idx( vi_x, vi_y ) ].id == mat_id_water ) && 
			  !g_world_particle_data[ compute_idx( vi_x, vi_y ) ].has_been_updated_this_frame ) ) ) ) ) {

		particle_t tmp_b = g_world_particle_data[ compute_idx( vi_x, vi_y ) ];

		// Try to throw water out
		if ( tmp_b.id == mat_id_water ) {

			tmp_b.has_been_updated_this_frame = true;

			s32 rx = random_val( -2, 2 );
			tmp_b.velocity = (gs_vec2){ rx, -3.f };

			write_data( compute_idx( vi_x, vi_y ), tmp_a );
			write_data( read_idx, tmp_b );

		}
		else if ( is_empty( vi_x, vi_y ) ) {
			write_data( compute_idx( vi_x, vi_y ), tmp_a );
			write_data( read_idx, tmp_b );
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if ( in_bounds( x, y + 1 ) && (( is_empty( x, y + 1 ) || ( g_world_particle_data[ b_idx ].id == mat_id_water ) ) ) ) {
		p->velocity.y += (gravity * dt );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ b_idx ];
		write_data( b_idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y + 1 ) && (( is_empty( x - 1, y + 1 ) || g_world_particle_data[ bl_idx ].id == mat_id_water )) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y += (gravity * dt );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ bl_idx ];
		write_data( bl_idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x + 1, y + 1 ) && (( is_empty( x + 1, y + 1 ) || g_world_particle_data[ br_idx ].id == mat_id_water )) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y += (gravity * dt );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ br_idx ];
		write_data( br_idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	// Can move if in liquid
	else if ( in_bounds( x + 1, y ) && ( g_world_particle_data[ compute_idx( x + 1, y ) ].id == mat_id_water ) ) {
		u32 idx = compute_idx( x + 1, y );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y ) && ( g_world_particle_data[ compute_idx( x - 1, y ) ].id == mat_id_water ) ) {
		u32 idx = compute_idx( x - 1, y );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
}

void update_sand( u32 x, u32 y )
{
	f32 dt = gs_engine_instance()->ctx.platform->time.delta;

	// For water, same as sand, but we'll check immediate left and right as well
	u32 read_idx = compute_idx( x, y );
	particle_t* p = &g_world_particle_data[ read_idx ];
	u32 write_idx = read_idx;
	u32 fall_rate = 4;

	p->velocity.y = gs_clamp( p->velocity.y + (gravity * dt), -10.f, 10.f );
	p->velocity.x = gs_clamp( p->velocity.x, -5.f, 5.f );

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if ( in_bounds( x, y + 1 ) && !is_empty( x, y + 1 ) && get_particle_at( x, y + 1 ).id != mat_id_water && get_particle_at( x, y + 1 ).id != mat_id_sand ) {
		p->velocity.y /= 2.f;
	}

	s32 vi_x = x + (s32)p->velocity.x; 
	s32 vi_y = y + (s32)p->velocity.y;

	// Check to see if you can swap first with other element below you
	u32 b_idx = compute_idx( x, y + 1 );
	u32 br_idx = compute_idx( x + 1, y + 1 );
	u32 bl_idx = compute_idx( x - 1, y + 1 );

	particle_t tmp_a = g_world_particle_data[ read_idx ];

	// Physics (using velocity)
	if ( in_bounds( vi_x, vi_y ) && (( is_empty( vi_x, vi_y ) ||
			((( g_world_particle_data[ compute_idx( vi_x, vi_y ) ].id == mat_id_water ) && 
			  !g_world_particle_data[ compute_idx( vi_x, vi_y ) ].has_been_updated_this_frame && 
			   gs_vec2_len(g_world_particle_data[compute_idx(vi_x, vi_y)].velocity) - gs_vec2_len(tmp_a.velocity) > 10.f ) ) ) ) ) {

		particle_t tmp_b = g_world_particle_data[ compute_idx( vi_x, vi_y ) ];

		// Try to throw water out
		if ( tmp_b.id == mat_id_water ) {

			tmp_b.has_been_updated_this_frame = true;

			s32 rx = random_val( -2, 2 );
			tmp_b.velocity = (gs_vec2){ rx, -3.f };

			write_data( compute_idx( vi_x, vi_y ), tmp_a );	

			for( s32 i = -10; i < 0; ++i ) {
				for ( s32 j = -5; j < 5; ++j ) {
					if ( is_empty( vi_x + j, vi_y + i ) ) {
						write_data( compute_idx( vi_x + j, vi_y + i ), tmp_b );
						break;
					}	
				}
			}

			// Couldn't write there, so, uh, destroy it.
			write_data( read_idx, particle_empty() );
		}
		else if ( is_empty( vi_x, vi_y ) ) {
			write_data( compute_idx( vi_x, vi_y ), tmp_a );
			write_data( read_idx, tmp_b );
		}
	}
	else if ( in_bounds( vi_x, vi_y ) && (( is_empty( vi_x, vi_y ) ||
			((( g_world_particle_data[ compute_idx( vi_x, vi_y ) ].id == mat_id_water ) && 
			  !g_world_particle_data[ compute_idx( vi_x, vi_y ) ].has_been_updated_this_frame ) ) ) ) ) {

		particle_t tmp_b = g_world_particle_data[ compute_idx( vi_x, vi_y ) ];

		// Try to throw water out
		if ( tmp_b.id == mat_id_water ) {

			tmp_b.has_been_updated_this_frame = true;

			s32 rx = random_val( -2, 2 );
			tmp_b.velocity = (gs_vec2){ rx, -3.f };

			write_data( compute_idx( vi_x, vi_y ), tmp_a );
			write_data( read_idx, tmp_b );

		}
		else if ( is_empty( vi_x, vi_y ) ) {
			write_data( compute_idx( vi_x, vi_y ), tmp_a );
			write_data( read_idx, tmp_b );
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if ( in_bounds( x, y + 1 ) && (( is_empty( x, y + 1 ) || ( g_world_particle_data[ b_idx ].id == mat_id_water ) ) ) ) {
		p->velocity.y += (gravity * dt );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ b_idx ];
		write_data( b_idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y + 1 ) && (( is_empty( x - 1, y + 1 ) || g_world_particle_data[ bl_idx ].id == mat_id_water )) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y += (gravity * dt );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ bl_idx ];
		write_data( bl_idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x + 1, y + 1 ) && (( is_empty( x + 1, y + 1 ) || g_world_particle_data[ br_idx ].id == mat_id_water )) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y += (gravity * dt );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ br_idx ];
		write_data( br_idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	// Can move if in liquid
	else if ( in_bounds( x + 1, y ) && ( g_world_particle_data[ compute_idx( x + 1, y ) ].id == mat_id_water ) ) {
		u32 idx = compute_idx( x + 1, y );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y ) && ( g_world_particle_data[ compute_idx( x - 1, y ) ].id == mat_id_water ) ) {
		u32 idx = compute_idx( x - 1, y );
		particle_t tmp_a = g_world_particle_data[ read_idx ];
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, tmp_a );
		write_data( read_idx, tmp_b );
	}
}

void update_smoke( u32 x, u32 y )
{
	f32 dt = gs_engine_instance()->ctx.platform->time.delta;

	// For water, same as sand, but we'll check immediate left and right as well
	u32 read_idx = compute_idx( x, y );
	particle_t* p = &g_world_particle_data[ read_idx ];
	u32 write_idx = read_idx;
	u32 fall_rate = 4;

	if ( p->life_time > 10.f ) {
		write_data( read_idx, particle_empty() );
		return;
	}

	if ( p->has_been_updated_this_frame ) {
		return;
	}

	p->has_been_updated_this_frame = true;

	// Smoke rises over time. This might cause issues, actually...
	p->velocity.y = gs_clamp( p->velocity.y - (gravity * dt ), -2.f, 10.f );
	p->velocity.x = gs_clamp( p->velocity.x + (f32)random_val( -100, 100 ) / 100.f, -1.f, 1.f );

	// Change color based on life_time
	p->color.r = (u8)( gs_clamp( (gs_interp_linear( 10.f, 0.f, p->life_time * 0.5f ) / 10.f) * 150.f, 0.f, 150.f ) );
	p->color.g = (u8)( gs_clamp( (gs_interp_linear( 10.f, 0.f, p->life_time * 0.5f ) / 10.f) * 120.f, 0.f, 120.f ) );
	p->color.b = (u8)( gs_clamp( (gs_interp_linear( 10.f, 0.f, p->life_time * 0.5f ) / 10.f) * 100.f, 0.f, 100.f ) );

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if ( in_bounds( x, y - 1 ) && !is_empty( x, y - 1 ) && get_particle_at( x, y - 1 ).id != mat_id_water ) {
		p->velocity.y /= 2.f;
	}

	s32 vi_x = x + (s32)p->velocity.x; 
	s32 vi_y = y + (s32)p->velocity.y;

	if ( in_bounds( vi_x, vi_y ) && ( (is_empty( vi_x, vi_y ) || get_particle_at( vi_x, vi_y ).id == mat_id_water || get_particle_at( vi_x, vi_y ).id == mat_id_fire ) ) ) {

		particle_t tmp_b = g_world_particle_data[ compute_idx( vi_x, vi_y ) ];

		// Try to throw water out
		if ( tmp_b.id == mat_id_water ) {

			tmp_b.has_been_updated_this_frame = true;

			s32 rx = random_val( -2, 2 );
			tmp_b.velocity = (gs_vec2){ rx, -3.f };

			write_data( compute_idx( vi_x, vi_y ), *p );
			write_data( read_idx, tmp_b );

		}
		else if ( is_empty( vi_x, vi_y ) ) {
			write_data( compute_idx( vi_x, vi_y ), *p );
			write_data( read_idx, tmp_b );
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if ( in_bounds( x, y - 1 ) && (( is_empty( x, y - 1 ) || ( get_particle_at( x, y - 1 ).id == mat_id_water ) || get_particle_at(x,y-1).id == mat_id_fire ) ) ) {
		p->velocity.y -= (gravity * dt );
		particle_t tmp_b = get_particle_at( x, y - 1 );
		write_data( compute_idx( x, y - 1 ), *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y - 1 ) && (( is_empty( x - 1, y - 1 ) || get_particle_at( x - 1, y - 1 ).id == mat_id_water ) || get_particle_at(x-1, y-1).id == mat_id_fire) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt );
		particle_t tmp_b = get_particle_at( x - 1, y - 1 );
		write_data( compute_idx( x - 1, y - 1 ), *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x + 1, y - 1 ) && (( is_empty( x + 1, y - 1 ) || get_particle_at( x + 1, y - 1 ).id == mat_id_water ) || get_particle_at(x+1, y+1).id == mat_id_fire) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt );
		particle_t tmp_b = get_particle_at( x + 1, y - 1 );
		write_data( compute_idx( x + 1, y - 1 ), *p );
		write_data( read_idx, tmp_b );
	}
	// Can move if in liquid
	else if ( in_bounds( x + 1, y ) && ( get_particle_at( x + 1, y ).id == mat_id_water  ) ) {
		u32 idx = compute_idx( x + 1, y );
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y ) && ( g_world_particle_data[ compute_idx( x - 1, y ) ].id == mat_id_water ) ) {
		u32 idx = compute_idx( x - 1, y );
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else {
		write_data( read_idx, *p );
	}
}
void update_ember( u32 x, u32 y )
{
	f32 dt = gs_engine_instance()->ctx.platform->time.delta;

	// For water, same as sand, but we'll check immediate left and right as well
	u32 read_idx = compute_idx( x, y );
	particle_t* p = &g_world_particle_data[ read_idx ];
	u32 write_idx = read_idx;
	u32 fall_rate = 4;

	if ( p->life_time > 0.5f ) {
		write_data( read_idx, particle_empty() );
		return;
	}

	if ( p->has_been_updated_this_frame ) {
		return;
	}

	p->has_been_updated_this_frame = true;

	p->velocity.y = gs_clamp( p->velocity.y - (gravity * dt ), -2.f, 10.f );
	p->velocity.x = gs_clamp( p->velocity.x + (f32)random_val( -100, 100 ) / 100.f, -1.f, 1.f );

	// If directly on top of some wall, then replace it
	if ( in_bounds( x, y + 1 ) && get_particle_at( x, y + 1 ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x, y + 1 ), particle_fire() );	
	}
	else if ( in_bounds( x + 1, y + 1 ) && get_particle_at( x + 1, y + 1 ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x + 1, y + 1 ), particle_fire() );	
	}
	else if ( in_bounds( x - 1, y + 1 ) && get_particle_at( x - 1, y + 1 ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x - 1, y + 1 ), particle_fire() );	
	}
	else if ( in_bounds( x - 1, y ) && get_particle_at( x - 1, y ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x - 1, y ), particle_fire() );	
	}
	else if ( in_bounds( x + 1, y ) && get_particle_at( x + 1, y ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x + 1, y ), particle_fire() );	
	}
	else if ( in_bounds( x + 1, y - 1 ) && get_particle_at( x + 1, y - 1 ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x + 1, y - 1 ), particle_fire() );	
	}
	else if ( in_bounds( x - 1, y - 1 ) && get_particle_at( x - 1, y - 1 ).id == mat_id_brick && random_val( 0, 200 ) == 0 ) {
		write_data( compute_idx( x - 1, y - 1 ), particle_fire() );	
	}

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if ( in_bounds( x, y - 1 ) && !is_empty( x, y - 1 ) && get_particle_at( x, y - 1 ).id != mat_id_water ) {
		p->velocity.y /= 2.f;
	}

	s32 vi_x = x + (s32)p->velocity.x; 
	s32 vi_y = y + (s32)p->velocity.y;

	if ( in_bounds( vi_x, vi_y ) && ( is_empty( vi_x, vi_y ) || 
			get_particle_at( vi_x, vi_y ).id == mat_id_water ||
			get_particle_at( vi_x, vi_y ).id == mat_id_fire || 
			get_particle_at( vi_x, vi_y ).id == mat_id_smoke || 
			get_particle_at( vi_x, vi_y ).id == mat_id_ember ) ) {

		particle_t tmp_b = g_world_particle_data[ compute_idx( vi_x, vi_y ) ];

		// Try to throw water out
		if ( tmp_b.id == mat_id_water ) {

			tmp_b.has_been_updated_this_frame = true;

			s32 rx = random_val( -2, 2 );
			tmp_b.velocity = (gs_vec2){ rx, -3.f };

			write_data( compute_idx( vi_x, vi_y ), *p );
			write_data( read_idx, tmp_b );

		}
		else if ( is_empty( vi_x, vi_y ) ) {
			write_data( compute_idx( vi_x, vi_y ), *p );
			write_data( read_idx, tmp_b );
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if ( in_bounds( x, y - 1 ) && (( is_empty( x, y - 1 ) || ( get_particle_at( x, y - 1 ).id == mat_id_water ) || get_particle_at(x,y-1).id == mat_id_fire ) ) ) {
		p->velocity.y -= (gravity * dt );
		particle_t tmp_b = get_particle_at( x, y - 1 );
		write_data( compute_idx( x, y - 1 ), *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y - 1 ) && (( is_empty( x - 1, y - 1 ) || get_particle_at( x - 1, y - 1 ).id == mat_id_water ) || get_particle_at(x-1, y-1).id == mat_id_fire) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt );
		particle_t tmp_b = get_particle_at( x - 1, y - 1 );
		write_data( compute_idx( x - 1, y - 1 ), *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x + 1, y - 1 ) && (( is_empty( x + 1, y - 1 ) || get_particle_at( x + 1, y - 1 ).id == mat_id_water ) || get_particle_at(x+1, y+1).id == mat_id_fire) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt );
		particle_t tmp_b = get_particle_at( x + 1, y - 1 );
		write_data( compute_idx( x + 1, y - 1 ), *p );
		write_data( read_idx, tmp_b );
	}
	// Can move if in liquid
	else if ( in_bounds( x + 1, y ) && ( is_empty( x + 1, y ) || get_particle_at( x + 1, y ).id == mat_id_fire  ) ) {
		u32 idx = compute_idx( x + 1, y );
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y ) && ( is_empty( x - 1, y ) || get_particle_at( x - 1, y ).id == mat_id_fire ) ) {
		u32 idx = compute_idx( x - 1, y );
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else {
		write_data( read_idx, *p );
	}
}

void update_fire( u32 x, u32 y )
{
	f32 dt = gs_engine_instance()->ctx.platform->time.delta;

	// For water, same as sand, but we'll check immediate left and right as well
	u32 read_idx = compute_idx( x, y );
	particle_t* p = &g_world_particle_data[ read_idx ];
	u32 write_idx = read_idx;
	u32 fall_rate = 4;

	if ( p->life_time > 10.f ) {
		if ( random_val( 0, 100 ) == 0 ) {
			write_data( read_idx, particle_empty() );
			return;
		}
	}

	p->velocity.y = gs_clamp( p->velocity.y + (gravity * dt), -10.f, 10.f );
	p->velocity.x = gs_clamp( p->velocity.x, -5.f, 5.f );

	// Change color based on life_time
	// p->color.r = (u8)( gs_clamp( (gs_interp_linear( 0.f, 10000.f, p->life_time ) / 10000.f) * 255.f, 0.f, 255.f ) );

	if ( random_val( 0, (s32)(p->life_time * 100.f) ) % 100 == 0 ) {
		s32 ran = random_val( 0, 3 );
		switch ( ran ) {
			case 0: p->color = (color_t){ 255, 80, 20, 255 }; break;
			case 1: p->color = (color_t){ 250, 150, 10, 255 }; break;
			case 2: p->color = (color_t){ 200, 150, 0, 255 }; break;
			case 3: p->color = (color_t){ 100, 50, 2, 255 }; break;
		}
	}

	// How should fire update? 
	// It can only survive with fuel. After enough time burning, it will die unless more fire can be made by consuming fuel.
	// It should spawn embers as well as smoke.

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if ( in_bounds( x, y + 1 ) && !is_empty( x, y + 1 ) && ( get_particle_at( x, y + 1 ).id != mat_id_water || get_particle_at( x, y + 1 ).id != mat_id_smoke ) ) {
		p->velocity.y /= 2.f;
	}

	if ( random_val( 0, 10 ) == 0 ) {
		p->velocity.x = gs_clamp( p->velocity.x + (f32)random_val( -0.5f, 0.5f ), -1.f, 1.f );
	}
	p->velocity.x = gs_clamp( p->velocity.x, -0.5f, 0.5f );

	// Kill fire underneath
	if ( in_bounds( x, y + 3 ) && get_particle_at(x, y + 3).id == mat_id_fire && random_val(0, 100) == 0 ) {
		write_data( compute_idx(x, y +3 ), *p);
		write_data( read_idx, particle_empty() );
		return;
	}

	// Chance to kick itself up
	if ( in_bounds( x, y + 1 ) && get_particle_at( x, y + 1 ).id == mat_id_fire && 
		in_bounds( x, y - 1 ) && get_particle_at( x, y - 1 ).id == mat_id_empty ) {
		if ( random_val( 0, 5 ) == 0 * p->life_time < 2.f && p->life_time > 1.f ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -4; i < 0; ++i ) {
				for ( s32 j = r ? -2 : 2; r ? j < 2 : j > -2; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						write_data( compute_idx( x + rx, y + ry ), *p );
						write_data( read_idx, particle_empty() );
						break;
					}
				}
			}
		}
		return;
	}

	s32 vi_x = x + (s32)p->velocity.x; 
	s32 vi_y = y + (s32)p->velocity.y;

	// Check to see if you can swap first with other element below you
	u32 b_idx = compute_idx( x, y + 1 );
	u32 br_idx = compute_idx( x + 1, y + 1 );
	u32 bl_idx = compute_idx( x - 1, y + 1 );

	// Chance to spawn smoke above
	for ( u32 i = 0; i < random_val( 1, 10 ); ++i ) {
		if ( random_val( 0, 500 ) == 0 ) {
			if ( in_bounds( x, y - 1 ) && is_empty( x, y - 1 ) ) {
				write_data( compute_idx( x, y - 1 ), particle_smoke() );
			}
			else if ( in_bounds( x + 1, y - 1 ) && is_empty( x + 1, y - 1 ) ) {
				write_data( compute_idx( x + 1, y - 1 ), particle_smoke() );
			}
			else if ( in_bounds( x - 1, y - 1 ) && is_empty( x - 1, y - 1 ) ) {
				write_data( compute_idx( x - 1, y - 1 ), particle_smoke() );
			}
		}
	}

	// Spawn embers
	if ( random_val( 0, 250 ) == 0 && p->life_time < 3.f ) {
		for ( u32 i = 0; i < random_val(1, 100); ++i ) {
			if ( in_bounds( x, y - 1 ) && is_empty( x, y - 1 ) ) {
				particle_t e = particle_ember();
				e.velocity = (gs_vec2){ (f32)random_val(-5, 5) / 5.f, -(f32)random_val(2, 10) / 2.f };
				write_data( compute_idx( x, y - 1 ), e );
			}
			else if ( in_bounds( x + 1, y - 1 ) && is_empty( x + 1, y - 1 ) ) {
				particle_t e = particle_ember();
				e.velocity = (gs_vec2){ (f32)random_val(-5, 5) / 5.f, -(f32)random_val(2, 10) / 2.f };
				write_data( compute_idx( x + 1, y - 1 ), e );
			}
			else if ( in_bounds( x - 1, y - 1 ) && is_empty( x - 1, y - 1 ) ) {
				particle_t e = particle_ember();
				e.velocity = (gs_vec2){ (f32)random_val(-5, 5) / 5.f, -(f32)random_val(2, 10) / 2.f };
				write_data( compute_idx( x - 1, y - 1 ), e );
			}
		}
	}

	// If directly on top of some wall, then replace it
	if ( in_bounds( x, y + 1 ) && get_particle_at( x, y + 1 ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x, y + 1 ), particle_fire() );
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}
	else if ( in_bounds( x + 1, y + 1 ) && get_particle_at( x + 1, y + 1 ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x + 1, y + 1 ), particle_fire() );	
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}
	else if ( in_bounds( x - 1, y + 1 ) && get_particle_at( x - 1, y + 1 ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x - 1, y + 1 ), particle_fire() );	
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}
	else if ( in_bounds( x - 1, y ) && get_particle_at( x - 1, y ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x - 1, y ), particle_fire() );	
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}
	else if ( in_bounds( x + 1, y ) && get_particle_at( x + 1, y ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x + 1, y ), particle_fire() );	
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}
	else if ( in_bounds( x + 1, y - 1 ) && get_particle_at( x + 1, y - 1 ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x + 1, y - 1 ), particle_fire() );	
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}
	else if ( in_bounds( x - 1, y - 1 ) && get_particle_at( x - 1, y - 1 ).id == mat_id_brick && random_val( 0, 500 ) == 0 ) {
		write_data( compute_idx( x - 1, y - 1 ), particle_fire() );	
		if ( random_val( 0, 5 ) == 0 ) {
			s32 r = random_val(0, 1);
			for ( s32 i = -3; i < 2; ++i ) {
				for ( s32 j = r ? -3 : 2; r ? j < 2 : j > -3; r ? ++j : --j ) {
					s32 rx = j, ry = i;
					if ( in_bounds( x + rx, y + ry ) && is_empty( x + rx, y + ry ) ) {
						particle_t fp = particle_fire();
						write_data( compute_idx( x + rx, y + ry ), fp );
						break;
					}
				}
			}
		}
	}

	if ( in_bounds( vi_x, vi_y ) && (is_empty(vi_x, vi_y) || 
									get_particle_at(vi_x, vi_y).id == mat_id_fire ||
									get_particle_at(vi_x, vi_y).id == mat_id_smoke))
	{
		p->velocity.y += (gravity * dt );
		particle_t tmp_b = g_world_particle_data[ compute_idx(vi_x, vi_y) ];
		write_data( compute_idx(vi_x, vi_y), *p );
		write_data( read_idx, tmp_b );
	}

	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if ( in_bounds( x, y + 1 ) && (( is_empty( x, y + 1 ) || get_particle_at(x, y +1).id == mat_id_fire || ( g_world_particle_data[ b_idx ].id == mat_id_water ) ) ) ) {
		p->velocity.y += (gravity * dt );
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		particle_t tmp_b = g_world_particle_data[ b_idx ];
		write_data( b_idx, *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x - 1, y + 1 ) && (( is_empty( x - 1, y + 1 ) || get_particle_at(x-1, y+1).id == mat_id_fire || g_world_particle_data[ bl_idx ].id == mat_id_water )) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt );
		particle_t tmp_b = g_world_particle_data[ bl_idx ];
		write_data( bl_idx, *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x + 1, y + 1 ) && (( is_empty( x + 1, y + 1 ) || get_particle_at(x+1, y+1).id == mat_id_fire || g_world_particle_data[ br_idx ].id == mat_id_water )) ) {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt );
		particle_t tmp_b = g_world_particle_data[ br_idx ];
		write_data( br_idx, *p );
		write_data( read_idx, tmp_b );
	}
	// Can move if in liquid
	// else if ( in_bounds( x + 1, y ) && ( g_world_particle_data[ compute_idx( x + 1, y ) ].id == mat_id_water ) || get_particle_at(x-1, y).id == mat_id_fire ) {
	// 	u32 idx = compute_idx( x + 1, y );
	// 	p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
	// 	particle_t tmp_b = g_world_particle_data[ idx ];
	// 	write_data( idx, *p );
	// 	write_data( read_idx, tmp_b );
	// }
	// else if ( in_bounds( x - 1, y ) && ( g_world_particle_data[ compute_idx( x - 1, y ) ].id == mat_id_water || get_particle_at(x-1, y).id == mat_id_fire ) ) {
	// 	u32 idx = compute_idx( x - 1, y );
	// 	p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
	// 	particle_t tmp_b = g_world_particle_data[ idx ];
	// 	write_data( idx, *p );
	// 	write_data( read_idx, tmp_b );
	// }
	else if ( in_bounds( x - 1, y - 1 ) && ( g_world_particle_data[ compute_idx( x - 1, y - 1 ) ].id == mat_id_water || get_particle_at(x-1, y - 1).id == mat_id_fire ) ) {
		u32 idx = compute_idx( x - 1, y - 1 );
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x + 1, y - 1 ) && ( g_world_particle_data[ compute_idx( x + 1, y - 1 ) ].id == mat_id_water || get_particle_at(x-1, y - 1).id == mat_id_fire ) ) {
		u32 idx = compute_idx( x + 1, y - 1 );
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else if ( in_bounds( x, y - 1 ) && ( g_world_particle_data[ compute_idx( x, y - 1 ) ].id == mat_id_water || get_particle_at(x-1, y - 1).id == mat_id_fire ) ) {
		u32 idx = compute_idx( x, y - 1 );
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		particle_t tmp_b = g_world_particle_data[ idx ];
		write_data( idx, *p );
		write_data( read_idx, tmp_b );
	}
	else {
		p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		write_data( read_idx, *p );
	}
}

void update_water( u32 x, u32 y )
{
	f32 dt = gs_engine_instance()->ctx.platform->time.delta;

	u32 read_idx = compute_idx( x, y );
	particle_t* p = &g_world_particle_data[ read_idx ];
	u32 write_idx = read_idx;
	u32 fall_rate = 2;
	u32 spread_rate = 2;

	p->velocity.y = gs_clamp( p->velocity.y + (gravity * dt), -10.f, 10.f );

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if ( in_bounds( x, y + 1 ) && !is_empty( x, y + 1 ) && get_particle_at( x, y + 1 ).id != mat_id_water ) {
		p->velocity.y /= 2.f;
	}

	// Change color depending on pressure? Pressure would dictate how "deep" the water is, I suppose.

	s32 ran = random_val( 0, 1 );
	s32 r = ran ? spread_rate : -spread_rate;
	s32 l = -r;
	s32 u = fall_rate;
	s32 v_idx = compute_idx ( x + (s32)p->velocity.x, y + (s32)p->velocity.y );
	s32 b_idx = compute_idx( x, y + u );
	s32 bl_idx = compute_idx( x + l, y + u );
	s32 br_idx = compute_idx( x + r, y + u );
	s32 l_idx = compute_idx( x + l, y );
	s32 r_idx = compute_idx( x + r, y );

	if ( is_empty( x + (s32)p->velocity.x, y + (s32)p->velocity.y ) ) {
		write_data( v_idx, *p );
		write_data( read_idx, particle_empty() );
	}
	else if ( is_empty( x, y + u ) ) {
		write_data( b_idx, *p );
		write_data( read_idx, particle_empty() );
	} 
	else if ( is_empty( x + r, y + u ) ) {
		write_data( br_idx, *p );
		write_data( read_idx, particle_empty() );
	}
	else if ( is_empty( x + l, y + u ) ) {
		write_data( bl_idx, *p );
		write_data( read_idx, particle_empty() );
	}
	else {
		particle_t tmp = *p;
		b32 found = false;
		for ( u32 i = 1; i < spread_rate; ++i )
		{
			if ( is_empty( x + l * i , y ) ) {
				write_data( l_idx, *p );
				write_data( read_idx, particle_empty() );
				found = true;
				break;
			}
			else if ( is_empty( x + r * i, y ) ) {
				write_data( r_idx, *p );
				write_data( read_idx, particle_empty() );
				found = true;
				break;
			}
		}
		if ( !found ) {
			write_data( read_idx, tmp );
		}
	}
}

void update_default( u32 w, u32 h )
{
	u32 read_idx = compute_idx( w, h );
	write_data( read_idx, get_particle_at( w, h ) );
}

particle_t particle_empty()
{
	particle_t p = {0};
	p.id = mat_id_empty;
	p.color = mat_col_empty;
	return p;
}

particle_t particle_sand()
{
	particle_t p = {0};
	p.id = mat_id_sand;
	p.color = mat_col_sand;
	return p;
}

particle_t particle_water()
{
	particle_t p = {0};
	p.id = mat_id_water;
	p.color = mat_col_water;
	return p;
}

particle_t particle_salt()
{
	particle_t p = {0};
	p.id = mat_id_salt;
	p.color = mat_col_salt;
	return p;
}

particle_t particle_brick()
{
	particle_t p = {0};
	p.id = mat_id_brick;
	p.color = mat_col_brick;
	return p;
}

particle_t particle_fire()
{
	particle_t p = {0};
	p.id = mat_id_fire;
	p.color = mat_col_fire;
	return p;
}

particle_t particle_ember()
{
	particle_t p = {0};
	p.id = mat_id_ember;
	p.color = mat_col_ember;
	return p;
}

particle_t particle_smoke()
{
	particle_t p = {0};
	p.id = mat_id_smoke;
	p.color = mat_col_smoke;
	return p;
}

/*
	// For each particle, check against collisions from other particles

	// For each cell, do checks
	
	 // Might as well do this the right way from the get go...

*/


void update_sim()
{
	// Check particle vs. particle collisions	
	// I think this is where double buffering MIGHT actually come in handy...

	// Naive way first

	// Loop through all particles, do collisions with all other particles. What could go wrong?
	// for ( u32 h = 0; h < g_texture_height; ++h )
	// {
	// 	for ( u32  w = 0; w < g_texture_width; ++w; )
	// 	{

	// 	}	
	// }

}

















