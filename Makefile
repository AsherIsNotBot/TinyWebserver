CXX      := g++
CXXFLAGS := -std=c++17 -Wall -O2 -pthread -I./inc
TARGET   := server
SRCDIR   := ./src
OBJDIR   := ./obj
SRCS     := $(wildcard $(SRCDIR)/*.cpp)
OBJS     := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
all: $(TARGET)
