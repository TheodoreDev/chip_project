#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "SDL.h"

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
} sdl_t;

typedef struct {
	uint32_t window_width;
	uint32_t window_height;
	uint32_t fg_color;
	uint32_t bg_color;
	uint32_t scale_factor;
	bool pixel_outlines;
} config_t;

typedef enum {
	QUIT,
	RUNNING,
	PAUSED,
} emulator_state_t;

typedef struct {
	uint16_t opcode;
	uint16_t NNN;
	uint8_t NN;
	uint8_t N;
	uint8_t X;
	uint8_t Y;
} instruction_t;

typedef struct {
	emulator_state_t state;
	uint8_t ram[4096];
	bool display[64*32];
	uint16_t stack[12];
	uint16_t *stack_ptr;
	uint8_t V[16];
	uint16_t I;
	uint16_t PC;
	uint8_t delay_timer;
	uint8_t sound_timer;
	bool keypad[16];
	const char *rom_name;
	instruction_t inst;
} chip8_t;

// Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config){
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0){
		SDL_Log("Could not initialize SDL subsystems ! %s\n", SDL_GetError());
		return false; // Error
	}

	sdl->window = SDL_CreateWindow(
		"CHIP8 Emulator", 
		SDL_WINDOWPOS_CENTERED, 
		SDL_WINDOWPOS_CENTERED, 
		config.window_width * config.scale_factor, 
		config.window_height * config.scale_factor, 
		0);
	if(!sdl->window){
		SDL_Log("Could not create window %s\n", SDL_GetError());
		return false;
	}

	sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
	if(!sdl->renderer){
		SDL_Log("Could not create renderer %s\n", SDL_GetError());
		return false;
	}

	return true; // Succes
}

bool set_config_from_args(config_t *config, const int argc, char **argv){
	*config = (config_t){
		.window_width = 64,
		.window_height = 32,
		.fg_color = 0xFFFFFFFF,
		.bg_color = 0x00000000,
		.scale_factor = 20,
		.pixel_outlines = false,
	};
	for(int i = 1; i < argc; i++){
		(void)argv[i];
	}
	return true;
}

bool init_chip8(chip8_t *chip8, const char rom_name[]){
	const uint32_t entry_point = 0x200;
	const uint8_t font[] = {
		0xF0, 0x90, 0x90, 0x90, 0xF0,  // 0
		0x29, 0x60, 0x20, 0x20, 0x70,  // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0,  // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0,  // 3
		0x90, 0x90, 0xF0, 0x10, 0x10,  // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0,  // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0,  // 6
		0xF0, 0x10, 0x20, 0x40, 0x40,  // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0,  // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0,  // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90,  // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0,  // B
		0xF0, 0x80, 0x80, 0x80, 0xF0,  // C
		0xE0, 0x90, 0x90, 0x90, 0xE0,  // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0,  // E
		0xF0, 0x80, 0xF0, 0x80, 0x80,  // F
	};
	memset(chip8, 0, sizeof(chip8_t));
	memcpy(&chip8->ram[0], font, sizeof(font));

	FILE *rom = fopen(rom_name, "rb");
	if(!rom){
		SDL_Log("Rom file %s is invalid or does not exist\n", rom_name);
		return false;
	}

	fseek(rom, 0, SEEK_END);
	const size_t rom_size = ftell(rom);
	const size_t max_size = sizeof chip8->ram - entry_point;
	rewind(rom);
	if(rom_size > max_size){
		SDL_Log("Rom file %s is too big !\n", rom_name);
		return false;
	}

	if(fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1){
		SDL_Log("Could not read the rom file %s into CHIP8 memory\n", rom_name);
		return false;
	}
	fclose(rom);

	chip8->state = RUNNING;
	chip8->PC = entry_point;
	chip8->rom_name = rom_name;
	chip8->stack_ptr = &chip8->stack[0];

	return true;
}

void final_cleanup(const sdl_t sdl){
	SDL_DestroyRenderer(sdl.renderer);
	SDL_DestroyWindow(sdl.window);
	SDL_Quit(); // Shutdown SDL subsystems
}

void clear_screen(const sdl_t sdl, const config_t config){
	const uint32_t r = (config.bg_color >> 24) & 0xFF;
	const uint32_t g = (config.bg_color >> 16) & 0xFF;
	const uint32_t b = (config.bg_color >> 8) & 0xFF;
	const uint32_t a = (config.bg_color >> 0) & 0xFF;

	SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
	SDL_RenderClear(sdl.renderer);
}

void update_screen(const sdl_t sdl,const config_t config, chip8_t *chip8){
	SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

	const uint32_t bg_r = (config.bg_color >> 24) & 0xFF;
	const uint32_t bg_g = (config.bg_color >> 16) & 0xFF;
	const uint32_t bg_b = (config.bg_color >> 8) & 0xFF;
	const uint32_t bg_a = (config.bg_color >> 0) & 0xFF;

	const uint32_t fg_r = (config.fg_color >> 24) & 0xFF;
	const uint32_t fg_g = (config.fg_color >> 16) & 0xFF;
	const uint32_t fg_b = (config.fg_color >> 8) & 0xFF;
	const uint32_t fg_a = (config.fg_color >> 0) & 0xFF;

	for(uint32_t i = 0; i < sizeof chip8->display; i++){
		rect.x = (i % config.window_width) * config.scale_factor;
		rect.y = (i / config.window_width) * config.scale_factor;

		if(chip8->display[i]){
			SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
			SDL_RenderFillRect(sdl.renderer, &rect);

			if(config.pixel_outlines){
				SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
				SDL_RenderDrawRect(sdl.renderer, &rect);
			}

		} else{
			SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
			SDL_RenderFillRect(sdl.renderer, &rect);
		}
	}
	SDL_RenderPresent(sdl.renderer);
}

void handle_input(chip8_t *chip8){
	SDL_Event event;
	while (SDL_PollEvent(&event)){
		switch (event.type){
			case SDL_QUIT:
				chip8->state = QUIT;
				return;
			case SDL_KEYDOWN:
			switch (event.key.keysym.sym){
				case SDLK_p:
					if(chip8->state == RUNNING){
						chip8->state = PAUSED;
						puts("==== PAUSED ====");
					} else {
						chip8->state = RUNNING;
						puts("==== RUNNING ====");
					}
					return;
				default :
					break;
			}
				break;
			case SDL_KEYUP:
				break;
			default:
				break;
		}
	}
}

#ifdef DEBUG
	void print_debug_info(chip8_t *chip8){
		printf("Adress : 0x%04X, Opcode : 0x%04X Desc : ", chip8->PC-2, chip8->inst.opcode);
		switch ((chip8->inst.opcode >> 12) & 0x0F){
			case 0x00:
				if(chip8->inst.NN == 0xE0){
					printf("Clear screen\n");
				} else if(chip8->inst.NN == 0xEE){
					printf("Return from subroutine to adress 0x%04X\n", *(chip8->stack_ptr - 1));
				}else {
					printf("Unimplemented Opcode.\n");
				}
				break;
			case 0x01:
				printf("Jump to address NNN (0x%04X)\n", chip8->inst.NNN);
				break;
			case 0x02:
				printf("Call subroutine at NNN (0x%04X)\n", chip8->inst.NNN);
				break;
			case 0x03:
				printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
				break;
			case 0x04:
				printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
				break;
			case 0x05:
				printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
				break;
			case 0x06:
				printf("Set register v%X = NN (0x%02X)\n", chip8->inst.X, chip8->inst.NN);
				break;
			case 0x07:
				printf("Set register v%X (0x%02X) += NN (0x%02X). Result 0x%02X\n", 
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN, chip8->V[chip8->inst.X] + chip8->inst.NN);
			case 0x08:
				switch (chip8->inst.N){
					case 0:
						printf("Set register v%X = V%X (0x%02X)\n", 
								chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
						break;
					case 1:
						printf("Set register v%X (0x%02X) |= V%X (0x%02X). Result : 0x%02X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y],
								chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
						break;
					case 2:
						printf("Set register v%X (0x%02X) &= V%X (0x%02X). Result : 0x%02X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y],
								chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
						break;
					case 3:
						printf("Set register v%X (0x%02X) ^= V%X (0x%02X). Result : 0x%02X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y],
								chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
						break;
					case 4:
						printf("Set register v%X (0x%02X) += V%X (0x%02X), VF = 1 if carry. Result : 0x%02X, VF = %X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y],
								chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
								((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
						break;
					case 5:
						printf("Set register v%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow. Result : 0x%02X, VF = %X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y],
								chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],
								(chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
						break;
					case 6:
						printf("Set register v%X (0x%02X) >>= 1, VF = shifted off bit (%X). Result : 0x%02X, VF = %X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] & 1,
								chip8->V[chip8->inst.X] >> 1);
						break;
					case 7:
						printf("Set register v%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow. Result : 0x%02X, VF = %X\n", 
								chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y], 
								chip8->inst.X, chip8->V[chip8->inst.X],
								chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
								(chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
						break;
					case 0xE:
						printf("Set register v%X (0x%02X) <<= 1, VF = shifted off bit (%X). Result : 0x%02X, VF = %X\n", 
								chip8->inst.X, chip8->V[chip8->inst.X], (chip8->V[chip8->inst.X] & 0x80) >> 7,
								chip8->V[chip8->inst.X] << 1);
						break;
					default:
						printf("Unimplemented Opcode.\n");
						break;
				}
				break;
			case 0x09:
				printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
						chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
				break;
			case 0x0A:
				printf("Set I to NNN (0x%04X)\n", chip8->inst.NNN);
				break;
			case 0x0B:
				printf("Set PC to V0 (0x%02X) + NNN (0x%04X). Result PC = 0x%04X\n", 
						chip8->V[0], chip8->inst.NNN, chip8->V[0] + chip8->inst.NNN);
				break;
			case 0x0C:
				printf("Set V%X = rand() %% 256 & NN (0x%02X)\n", chip8->V[chip8->inst.X], chip8->inst.NN);
				break;
			case 0x0D:
				printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) "
						"from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
						chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->I);
				break;
			default :
				printf("Unimplemented opcode.\n");
				break;
		}
	}
#endif

void emulate_instruction(chip8_t *chip8, const config_t config){
	chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC+1];
	chip8->PC += 2;

	chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
	chip8->inst.NN = chip8->inst.opcode & 0x0FF;
	chip8->inst.N = chip8->inst.opcode & 0x0F;
	chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
	chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
	print_debug_info(chip8);
#endif

	switch ((chip8->inst.opcode >> 12) & 0x0F){
		case 0x00:
			if(chip8->inst.NN == 0xE0){
				memset(&chip8->display[0], false, sizeof chip8->display);
			} else if(chip8->inst.NN == 0xEE){
				chip8->PC = *--chip8->stack_ptr;
			}
			break;
		case 0x01:
			chip8->PC = chip8->inst.NNN;
			break;
		case 0x02:
			*chip8->stack_ptr++ = chip8->PC;
			chip8->PC = chip8->inst.NNN;
			break;
		case 0x03:
			if(chip8->V[chip8->inst.X] == chip8->inst.NN){
				chip8->PC += 2;
			}
			break;
		case 0x04:
			if(chip8->V[chip8->inst.X] != chip8->inst.NN){
				chip8->PC += 2;
			}
			break;
		case 0x05:
			if(chip8->inst.N != 0) break;
			if(chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]){
				chip8->PC += 2;
			}
			break;
		case 0x06:
			chip8->V[chip8->inst.X] = chip8->inst.NN;
			break;
		case 0x07:
			chip8->V[chip8->inst.X] += chip8->inst.NN;
			break;
		case 0x08:
			switch (chip8->inst.N){
				case 0:
					chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
					break;
				case 1:
					chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
					break;
				case 2:
					chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
					break;
				case 3:
					chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
					break;
				case 4:
					if((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255)
						chip8->V[0xF] = 1;
					chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
					break;
				case 5:
					if(chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y])
						chip8->V[0xF] = 1;
					chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
					break;
				case 6:
					chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;
					chip8->V[chip8->inst.X] >>= 1;
					break;
				case 7:
					if(chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y])
						chip8->V[0xF] = 1;
					chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
					break;
				case 0xE:
					chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
					chip8->V[chip8->inst.X] <<= 1;
					break;
				default:
					break;
			}
			break;
		case 0x09:
			if(chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
				chip8->PC += 2;
			break;
		case 0x0A:
			chip8->I = chip8->inst.NNN;
			break;
		case 0x0B:
			chip8->PC = chip8->V[0] + chip8->inst.NNN;
			break;
		case 0x0C:
			chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
			break;
		case 0x0D: {
			uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
			uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
			const uint8_t orig_X = X_coord;

			chip8->V[0xF] = 0;
			for(uint8_t i = 0; i < chip8->inst.N; i++){
				const uint8_t sprite_data = chip8->ram[chip8->I + i];
				X_coord = orig_X;
				
				for(int8_t j = 7; j >= 0; j--){
					bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
					const bool sprite_bit = (sprite_data & (1 << j));
					if(sprite_bit && *pixel){
						chip8->V[0xF] = 1;
					}
					*pixel ^= sprite_bit;

					if(++X_coord >= config.window_width) break;
				}
				if(++Y_coord >= config.window_height) break;
			}
			break;
		}
		default :
			break;
	}
}

int main(int argc, char **argv){
	// Default usage message for args
	if(argc < 2){
		fprintf(stderr, "Usage : %s <rom_name>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// Initialize emulator config/options
	config_t config = {0};
	if(!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

	// Initialize SDL
	sdl_t sdl = {0};
	if(!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

	// Initial screen clear
	clear_screen(sdl, config);

	// Seed the random number generator
	srand(time(NULL));

	// Initialize CHIP8 machine
	chip8_t chip8 = {0};
	const char *rom_name = argv[1];
	if(!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

	// Main loop
	while (chip8.state != QUIT){
		handle_input(&chip8);
		if(chip8.state == PAUSED) continue;

		emulate_instruction(&chip8, config);

		SDL_Delay(16);

		update_screen(sdl, config, &chip8);
	}

	// Final cleanup
	final_cleanup(sdl);

	exit(EXIT_SUCCESS);
}