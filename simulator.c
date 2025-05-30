#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef LINUX
//chiant à installer, lien d'un tres bon tuto : https://www.youtube.com/watch?v=uv4fda8Z8Tk
    #include "inc/SDL.h"
#else
    #include <SDL2/SDL.h>
#endif


#define MAX_LINE 256
#define MAX_INSTRUCTIONS 9082
#define MAX_SIGNALS 8092

#define RAM_SIZE 65536
#define ROM_SIZE 65536

#define SCREEN_WIDTH 384
#define SCREEN_HEIGHT 256

#define VIDEO_RAM_START 1000
#define VIDEO_RAM_SIZE 6144

#define STEP_LIMIT -1

const int FPS = 60;
const int FRAME_TIME = 1000 / FPS;
const bool DEBUG = false;

typedef struct {
    int id;
    int type;
    int output;
    int input1;
    int input2;
    int input3;
    int size;
    int enabled_if;
    int const_value; // valeur compressée
    int has_const;
} Instruction;

enum {
    GATE_AND,
    GATE_OR,
    GATE_XOR,
    GATE_NAND,
    GATE_NOR,
    GATE_NXOR,
    GATE_NOT,
    GATE_CONST,
    GATE_MUX,
    GATE_CONCAT,
    GATE_INDEX,
    GATE_SUB,
    GATE_BUF,
    GATE_UNKNOWN,
    GATE_LOAD,
    GATE_STORE,
    GATE_ROM,
};

Instruction instructions[MAX_INSTRUCTIONS];
int instruction_count = 0;

int inputs[MAX_SIGNALS];
int input_count = 0;
int outputs[MAX_SIGNALS];
int output_count = 0;
int jump_table[MAX_INSTRUCTIONS];
int ram[RAM_SIZE];
int rom[RAM_SIZE];

int gate_type_from_string(const char* str) {
    if (strcmp(str, "AND") == 0) return GATE_AND;
    if (strcmp(str, "OR") == 0) return GATE_OR;
    if (strcmp(str, "XOR") == 0) return GATE_XOR;
    if (strcmp(str, "NAND") == 0) return GATE_NAND;
    if (strcmp(str, "NOR") == 0) return GATE_NOR;
    if (strcmp(str, "NXOR") == 0) return GATE_NXOR;
    if (strcmp(str, "NOT") == 0) return GATE_NOT;
    if (strcmp(str, "CONST") == 0) return GATE_CONST;
    if (strcmp(str, "MUX") == 0) return GATE_MUX;
    if (strcmp(str, "CONCAT") == 0) return GATE_CONCAT;
    if (strcmp(str, "INDEX") == 0) return GATE_INDEX;
    if (strcmp(str, "SUB") == 0) return GATE_SUB;
    if (strcmp(str, "BUF") == 0) return GATE_BUF;
    if (strcmp(str, "LOAD") == 0) return GATE_LOAD;
    if (strcmp(str, "STORE") == 0) return GATE_STORE;
    if (strcmp(str, "ROM") == 0) return GATE_ROM;
    return GATE_UNKNOWN;
}


void afficher_instruction(const Instruction* inst) {
    printf("ID %d : %d (%d <- %d %d %d)", inst->id, inst->type,
           inst->output, inst->input1, inst->input2, inst->input3);
    if (inst->has_const) {
        printf("  CONST = %d", inst->const_value);
    }
    printf("\n");
}

void print_binary(int value, int size) {
    for (int i = size - 1; i >= 0; i--) {
        printf("%d", (value >> i) & 1);
    }
}

void parse_line(const char* line) {
    Instruction inst = {0};
    inst.has_const = 0;

    char type_char[16];

    if (strncmp(line, "GateIR", 6) != 0) return;

    sscanf(line,
        "GateIR(id=%d, type='%15[^']', output=%d, input1=%d, input2=%d, input3=%d, size=%d, enabled_if=%d",
        &inst.id, type_char, &inst.output, &inst.input1, &inst.input2, &inst.input3, &inst.size, &inst.enabled_if);

        inst.type = gate_type_from_string(type_char);

        const char* pos = strstr(line, "const_value=");
        if (pos) {
            inst.has_const = 1;
            sscanf(pos, "const_value=%d", &inst.const_value);
        }
        

    instructions[instruction_count++] = inst;
}

void afficher_signaux(const char* label, int* table, int count) {
    printf("%s (%d) : ", label, count);
    for (int i = 0; i < count; i++) {
        printf("%d ", table[i]);
    }
    printf("\n");
}

void load_rom_text_binary(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Erreur à l'ouverture du fichier texte");
        exit(1);
    }

    char line[128];  // plus large au cas où il y a des espaces
    int i = 0;

    while (fgets(line, sizeof(line), file) && i < ROM_SIZE) {
        int value = 0;
        int bit_count = 0;

        for (int j = 0; line[j] != '\0' && bit_count < 32; j++) {
            if (line[j] == '0' || line[j] == '1') {
                value = (value << 1) | (line[j] - '0');
                bit_count++;
            }
            // Sinon : ignore les espaces, tabulations, retours chariot, etc.
        }

        rom[i++] = value;
    }

    fclose(file);

    while (i < ROM_SIZE) {
        rom[i++] = 0;
    }
}

Uint32* framebuffer = NULL;
int fb_pitch = 0;

void set_pixel(int x, int y, Uint32 color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    framebuffer[y * (fb_pitch / 4) + x] = color;
}



int main(int argc, char** argv) {

    printf("PROUT !\n");

    if (argc != 3) {
        printf("Usage: %s <fichier.ir> <program.rom>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        perror("Erreur d'ouverture du fichier");
        return 1;
    }

    int signal_count = 0;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "# SIGNALS:", 10) == 0) {
            sscanf(line, "# SIGNALS: %d", &signal_count);
            continue;
        }
        if (strncmp(line, "# INPUTS:", 9) == 0) {
            char buffer[MAX_LINE];
            strncpy(buffer, line + 9, MAX_LINE);
            char* token = strtok(buffer, ", \t\n");
            while (token && input_count < MAX_SIGNALS) {
                inputs[input_count++] = atoi(token);
                token = strtok(NULL, ", \t\n");
            }
            continue;
        }
        
        if (strncmp(line, "# OUTPUTS:", 10) == 0) {
            char buffer[MAX_LINE];
            strncpy(buffer, line + 10, MAX_LINE);
            char* token = strtok(buffer, ", \t\n");
            while (token && output_count < MAX_SIGNALS) {
                outputs[output_count++] = atoi(token);
                token = strtok(NULL, ", \t\n");
            }
            continue;
        }
        
        parse_line(line);
    }
    fclose(f);

    load_rom_text_binary(argv[2]);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Erreur SDL_Init : %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Fenêtre SDL2",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        printf("Erreur SDL_CreateWindow : %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT
    );
    void* pixels;
    SDL_LockTexture(texture, NULL, &pixels, &fb_pitch);
    memset(pixels, 0, fb_pitch * SCREEN_HEIGHT);
    framebuffer = (Uint32*)pixels;
    

    int last_block_start = 0;
    int last_index = -1;

    for (int jumpcpt = 0; jumpcpt<instruction_count; jumpcpt++){
        Instruction* inst = &instructions[jumpcpt];
        if(inst->enabled_if!=last_index){
            if(last_index!=-1){
                jump_table[last_block_start] = jumpcpt;
            }
            if (inst->enabled_if!=-1){
                last_block_start = jumpcpt;
            }
            last_block_start = jumpcpt;
            last_index = inst->enabled_if;
        }
        jump_table[jumpcpt] = jumpcpt+1;
    }

    printf("Instructions lues : %d\n", instruction_count);

    Uint32 start_time = SDL_GetTicks();
    SDL_Event event;

    if (signal_count > 0) {
        int* signals = calloc(signal_count, sizeof(int));
        if (!signals) {
            fprintf(stderr, "Erreur d'allocation mémoire pour %d signaux.\n", signal_count);
            return 1;
        }
        printf("Memoire pour %d signaux allouee avec succes.\n", signal_count);
        afficher_signaux("Entrees", inputs, input_count);
        afficher_signaux("Sorties", outputs, output_count);

        ram[0] = 1;
        int j = 0;

        while (ram[0] && j != STEP_LIMIT){

            for (int i = 0; i < input_count; i++) {
                printf("Entrez la valeur entiere de signal[%d] : ", inputs[i]);
                scanf("%d", &signals[inputs[i]]);
            }


            

            for (int i = 0; i < instruction_count;) {
                Instruction* inst = &instructions[i];

                if(inst->enabled_if>=0 && signals[inst->enabled_if]==0){
                    i = jump_table[i];
                    continue;
                }

                int mask = (1 << inst->size) - 1;

                int a = signals[inst->input1];
                int b = signals[inst->input2];
                int c = signals[inst->input3];

                switch (inst->type) {
                    case GATE_AND:
                        signals[inst->output] = (a & b) & mask;
                        break;
                    case GATE_OR:
                        signals[inst->output] = (a | b) & mask;
                        break;
                    case GATE_XOR:
                        signals[inst->output] = (a ^ b) & mask;
                        break;
                    case GATE_NAND:
                        signals[inst->output] = ~(a & b) & mask;
                        break;
                    case GATE_NOR:
                        signals[inst->output] = ~(a | b) & mask;
                        break;
                    case GATE_NXOR:
                        signals[inst->output] = ~(a ^ b) & mask;
                        break;
                    case GATE_NOT:
                        signals[inst->output] = ~a & mask;
                        break;
                    case GATE_CONST:
                        signals[inst->output] = inst->const_value & mask;
                        break;
                    case GATE_MUX:
                        signals[inst->output] = (a ? c : b) & mask;
                        break;
                    case GATE_CONCAT: {
                        int size_b = inst->const_value;
                        signals[inst->output] = (a << size_b) | b;
                        break;
                    }
                    case GATE_INDEX: {
                        int bit_index = inst->const_value;
                        signals[inst->output] = (a >> bit_index) & 1;
                        break;
                    }
                    case GATE_SUB: {
                        int start = inst->const_value;
                        int len = inst->size;
                        signals[inst->output] = (a >> start) & ((1 << len) - 1);
                        break;
                    }
                    case GATE_BUF:
                        signals[inst->output] = a & mask;
                        break;
                    case GATE_STORE:
                        ram[a] = b & mask;
                        if(DEBUG || a==42)printf("\nRam[%d] got value %d", a, ram[a]);
                        if(a>=VIDEO_RAM_START && a<VIDEO_RAM_START+VIDEO_RAM_SIZE){
                            for(int t=0;t<16;t++){
                                int bit = (b >> t) & 1;
                                int color = 0xFFFFFF*bit;
                                int pos = (a-VIDEO_RAM_START+1)*16-t-1;
                                set_pixel(pos/(SCREEN_HEIGHT*8)*8+pos%8,pos%(SCREEN_HEIGHT*8)/8,color);
                            }
                        }
                        break;
                    case GATE_LOAD:
                        signals[inst->output] = ram[a] & mask;
                        if(DEBUG || a==42){printf("\nRead Ram[%d] and got value %d", a, ram[a]);}
                        break;
                    case GATE_ROM:
                        signals[inst->output] = rom[a];
                        break;
                    default:
                        // handle unknown gate type
                        break;
                }
                i++;
            }    
            
            j++;
            

            if(output_count){printf("\nResultats des sorties :\n");}
            for (int i = 0; i < output_count; i++) {
                printf("signal[%d] = %d\n", outputs[i], signals[outputs[i]]);
            }

            if(ram[1]){
                SDL_UnlockTexture(texture);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
                Uint32 end_time = SDL_GetTicks();
                int elapsed = end_time - start_time;

                if (elapsed < FRAME_TIME) {
                    SDL_Delay(FRAME_TIME - elapsed);
                }
                start_time = SDL_GetTicks();
                SDL_LockTexture(texture, NULL, &pixels, &fb_pitch);
                ram[1] = 0;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        ram[0] = 0;
                    }
                    if (event.type == SDL_KEYDOWN) {
                        switch (event.key.keysym.sym) {
                            case SDLK_UP:
                                ram[10] = 1;
                                break;
                            case SDLK_DOWN:
                                ram[11] = 1;
                                break;
                            case SDLK_LEFT:
                                ram[12] = 1;
                                break;
                            case SDLK_RIGHT:
                                ram[13] = 1;
                                break;
                            case SDLK_SPACE:
                                ram[14] = 1;
                                break;
                        }
                    }
                    if (event.type == SDL_KEYUP) {
                        switch (event.key.keysym.sym) {
                            case SDLK_UP:
                                ram[10] = 0;
                                break;
                            case SDLK_DOWN:
                                ram[11] = 0;
                                break;
                            case SDLK_LEFT:
                                ram[12] = 0;
                                break;
                            case SDLK_RIGHT:
                                ram[13] = 0;
                                break;
                            case SDLK_SPACE:
                                ram[14] = 0;
                                break;
                        }
                    }
                }
            }

        }

        free(signals);
        SDL_UnlockTexture(texture);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_Delay(250);

        SDL_DestroyWindow(window);
        SDL_Quit();

    } else {
        printf("Aucune information sur les signaux trouvee.\n");
    }

    return 0;
}
