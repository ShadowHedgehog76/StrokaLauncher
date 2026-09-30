CC      ?= cc
HOSTCC  ?= cc
WINDRES ?= windres
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += -std=gnu11
BUILD   ?= build

# Identifiants Supabase (URL + clé publique), lus depuis supabase.env
-include supabase.env
SB_DEFS := -DSUPABASE_URL='"$(SUPABASE_URL)"' -DSUPABASE_KEY='"$(SUPABASE_KEY)"'

# ---------- plateforme ----------
ifeq ($(OS),Windows_NT)
  PLATFORM  := windows
  EXE       := .exe
else
  UNAME := $(shell uname -s)
  ifeq ($(UNAME),Darwin)
    PLATFORM := macos
  else
    PLATFORM := linux
  endif
  EXE :=
endif

ifeq ($(PLATFORM),macos)
  GUI_LIBS  := -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL \
               -framework CoreAudio -framework AudioToolbox
  CORE_LIBS := -lcurl -lz -lpthread
else ifeq ($(PLATFORM),windows)
  GUI_LIBS  := -lopengl32 -lgdi32 -lwinmm -mwindows
  CORE_LIBS := -lcurl -lz -lpthread -lws2_32 -lshell32 -lcomdlg32 -lole32
  WIN_RES   := $(BUILD)/stroka_res.o
else
  GUI_LIBS  := -lGL -lm -lpthread -ldl -lrt -lX11
  CORE_LIBS := -lcurl -lz -lpthread
endif

LAUNCHER := StrokaLauncher$(EXE)
CLI      := stroka-cli$(EXE)

CORE_SRC := src/auth.c src/game.c src/http.c src/java.c src/modmeta.c src/pack.c src/ping.c src/platform.c src/report.c \
            src/settings.c src/skin.c src/supabase.c src/sync.c src/usermods.c src/util.c src/zip.c
CJSON    := third_party/cjson/cJSON.c
GUI_SRC  := gui/app.c gui/brand.c gui/draw.c gui/scene.c gui/ui.c

# Décodeur WebP (icônes des mods Modrinth), sans l'encodeur
WEBP_DIR  := third_party/libwebp
WEBP_SRC  := $(wildcard $(WEBP_DIR)/src/dec/*.c) $(wildcard $(WEBP_DIR)/src/utils/*.c) \
             $(filter-out $(wildcard $(WEBP_DIR)/src/dsp/enc*.c $(WEBP_DIR)/src/dsp/cost*.c $(WEBP_DIR)/src/dsp/ssim*.c \
                                     $(WEBP_DIR)/src/dsp/lossless_enc*.c), $(wildcard $(WEBP_DIR)/src/dsp/*.c))
WEBP_OBJ  := $(WEBP_SRC:$(WEBP_DIR)/src/%.c=$(BUILD)/webp/%.o)
WEBP_LIB  := $(BUILD)/libwebpdec.a

RAYLIB_DIR := third_party/raylib/src
RAYLIB_LIB := $(RAYLIB_DIR)/libraylib.a

CORE_OBJ := $(CORE_SRC:%.c=$(BUILD)/%.o) $(BUILD)/cjson.o
GUI_OBJ  := $(GUI_SRC:%.c=$(BUILD)/%.o)
FONTS_H  := $(BUILD)/fonts_data.h
FONTS    := $(wildcard assets/fonts/*.ttf)
BIN2C    := $(BUILD)/bin2c-host

APP       := $(BUILD)/StrokaLauncher.app

# Outils privés (app admin), non publiés : règles dans admin/admin.mk s'il est présent
-include admin/admin.mk

all: $(LAUNCHER) $(CLI) $(PRIVATE_TARGETS)

# ---------- interface graphique ----------
$(LAUNCHER): $(GUI_OBJ) $(CORE_OBJ) $(RAYLIB_LIB) $(WEBP_LIB) $(WIN_RES)
	$(CC) $(CFLAGS) -o $@ $(GUI_OBJ) $(CORE_OBJ) $(WIN_RES) $(RAYLIB_LIB) $(WEBP_LIB) $(CORE_LIBS) $(GUI_LIBS)

$(BUILD)/gui/%.o: gui/%.c gui/*.h src/*.h $(FONTS_H) $(RAYLIB_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -I$(RAYLIB_DIR) -I$(BUILD) -I$(WEBP_DIR)/src -c -o $@ $<

$(WEBP_LIB): $(WEBP_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/webp/%.o: $(WEBP_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -I$(WEBP_DIR) -I$(WEBP_DIR)/src -c -o $@ $<

# ---------- ressources intégrées (polices, musique) ----------
$(BIN2C): tools/bin2c.c
	@mkdir -p $(BUILD)
	$(HOSTCC) -O2 -o $@ $<

$(FONTS_H): $(FONTS) $(BIN2C)
	@echo "/* Généré automatiquement depuis assets/fonts */" > $@
	@for f in $(FONTS); do $(BIN2C) $$f >> $@; done

$(RAYLIB_LIB):
	$(MAKE) -C $(RAYLIB_DIR) PLATFORM=PLATFORM_DESKTOP CC="$(CC)" CUSTOM_CFLAGS=-DSUPPORT_FILEFORMAT_JPG=1

# Icône et informations de l'exécutable Windows
$(BUILD)/stroka_res.o: packaging/windows/stroka.rc packaging/windows/stroka.ico
	@mkdir -p $(BUILD)
	$(WINDRES) $< -O coff -o $@

# ---------- version terminal ----------
$(CLI): $(BUILD)/src/main.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(CORE_LIBS)

# ---------- moteur ----------
$(BUILD)/src/%.o: src/%.c src/*.h $(wildcard supabase.env)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SB_DEFS) -c -o $@ $<

$(BUILD)/cjson.o: $(CJSON)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -w -c -o $@ $<

# ---------- application macOS ----------
# Icône : packaging/macos/icon.icns (versionnée) ; régénérable avec « make icon »
icon: $(LAUNCHER)
	@rm -rf $(BUILD)/icon.iconset && mkdir -p $(BUILD)/icon.iconset packaging/macos
	STROKA_ICON=$(CURDIR)/packaging/icon.png ./$(LAUNCHER)
	@for s in 16 32 128 256 512; do \
	  sips -z $$s $$s packaging/icon.png --out $(BUILD)/icon.iconset/icon_$${s}x$${s}.png >/dev/null; \
	  d=$$((s*2)); sips -z $$d $$d packaging/icon.png --out $(BUILD)/icon.iconset/icon_$${s}x$${s}@2x.png >/dev/null; \
	done
	iconutil -c icns -o packaging/macos/icon.icns $(BUILD)/icon.iconset

app: $(LAUNCHER)
	@rm -rf $(APP)
	@mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	cp $(LAUNCHER) $(APP)/Contents/MacOS/StrokaLauncher
	cp packaging/Info.plist $(APP)/Contents/Info.plist
	cp packaging/macos/icon.icns $(APP)/Contents/Resources/icon.icns
	@echo "Application créée : $(APP)"

clean:
	rm -rf $(BUILD) StrokaLauncher StrokaAdmin stroka-cli *.exe

distclean: clean
	$(MAKE) -C $(RAYLIB_DIR) clean

.PHONY: all app icon clean distclean
