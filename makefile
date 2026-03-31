CC       = gcc
CFLAGS   = -Wall -g -Iinclude
LDFLAGS  =

SRC_DIR  = src
OBJ_DIR  = obj
BIN_DIR  = bin
TMP_DIR  = tmp

SRCS     = $(wildcard $(SRC_DIR)/*.c)
OBJS     = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

all: folders controller runner

controller: $(BIN_DIR)/controller
runner:     $(BIN_DIR)/runner

folders:
	@mkdir -p $(SRC_DIR) $(OBJ_DIR) $(BIN_DIR) $(TMP_DIR) include

$(BIN_DIR)/controller: $(OBJ_DIR)/controller.o
	$(CC) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/runner: $(OBJ_DIR)/runner.o
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_DIR)/* $(TMP_DIR)/* $(BIN_DIR)/*
