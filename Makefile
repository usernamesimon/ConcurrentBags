NAME = concurrentBagsSimple

CC ?= gcc
RM ?= @rm
MKDIR ?= @mkdir

CFLAGS := -O3 -Wall -Wextra -fopenmp -latomic
CFLAGSD := -O0 -Wall -Wextra -fopenmp -latomic -ggdb

SRC_DIR = src
BUILD_DIR = build
DATA_DIR = data
INCLUDES = inc

OBJECTS = $(NAME).o
OBJECTSD = $(NAME).od


all: $(BUILD_DIR) $(NAME) $(NAME).so queue.so
	@echo "Built $(NAME)"

$(DATA_DIR):
	@echo "Creating data directory: $(DATA_DIR)"
	$(MKDIR) $(DATA_DIR)

$(BUILD_DIR):
	@echo "Creating build directory: $(BUILD_DIR)"
	$(MKDIR) $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c -o $@ $<

$(NAME): $(foreach object,$(OBJECTS),$(BUILD_DIR)/$(object))
	@echo "Linking $(NAME)"
	$(CC) $(CFLAGS) -o $@ $^

$(NAME).so: $(foreach object,$(OBJECTS),$(BUILD_DIR)/$(object))
	@echo "Linking $(NAME)"
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^ 

queue.so: $(SRC_DIR)/queue.c
	$(CC) $(CFLAGS) -fPIC -shared -o queue.so $(SRC_DIR)/queue.c

debug: $(BUILD_DIR) $(NAME).d $(NAME).sod
	@echo "Built $(NAME).d"

# $(DATA_DIR):
# 	@echo "Creating data directory: $(DATA_DIR)"
# 	$(MKDIR) $(DATA_DIR)

# $(BUILD_DIR):
# 	@echo "Creating build directory: $(BUILD_DIR)"
# 	$(MKDIR) $(BUILD_DIR)

$(BUILD_DIR)/%.od: $(SRC_DIR)/%.c
	@echo "Compiling $< in debug mode"
	$(CC) $(CFLAGSD) -fPIC -I$(INCLUDES) -c -o $@ $<

$(NAME).d: $(foreach object,$(OBJECTSD),$(BUILD_DIR)/$(object))
	@echo "Linking $@"
	$(CC) $(CFLAGSD) -o $@ $^

$(NAME).sod: $(foreach object,$(OBJECTSD),$(BUILD_DIR)/$(object))
	@echo "Linking $@"
	$(CC) $(CFLAGSD) -fPIC -shared -o $@ $^ 

bench:
	@echo "This could run a sophisticated benchmark"

plot:
	@echo "Plotting results from nebula_data"
	bash -c 'cd plots && \
	pdflatex "\newcommand{\DATAPATH}{../nebula_data/data/}\newcommand{\NUMCALLS}{100000}\input{latency.tex}" && \
	pdflatex "\newcommand{\DATAPATH}{../nebula_data/data/}\newcommand{\NUMCALLS}{100000}\input{throughput.tex}" && \
	pdflatex "\newcommand{\DATAPATH}{../nebula_data/data/}\newcommand{\NUMCALLS}{100000}\input{casplot.tex}" && \
	pdflatex "\newcommand{\DATAPATH}{../nebula_data/data/}\newcommand{\NUMCALLS}{100000}\input{steal.tex}"'
	

small-bench: $(BUILD_DIR) $(NAME).so $(DATA_DIR)
	@echo "Running small-bench ..."
	@python benchmark.py

small-plot: 
	@echo "Plotting small-bench results ..."
	bash -c 'cd plots && pdflatex "\newcommand{\DATAPATH}{../data/$$(ls ../data/ | sort -r | head -n 1)}\input{avg_plot.tex}"'
	@echo "============================================"
	@echo "Created plots/avgplot.pdf"

report: small-plot
	@echo "Compiling report ..."
	bash -c 'cd report && pdflatex report.tex'
	@echo "============================================"
	@echo "Done"

zip:
	@zip framework.zip benchmark.py Makefile README src/* plots/avg_plot.tex report/report.tex

clean:
	@echo "Cleaning build directory: $(BUILD_DIR) and binaries: $(NAME) $(NAME).so"
	$(RM) -Rf $(BUILD_DIR)
	$(RM) -f $(NAME) $(NAME).so
	$(RM) -f $(NAME).d $(NAME).sod
	$(RM) -f queue.so

.PHONY: clean report
