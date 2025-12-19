CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I/usr/include/fuse3
LDFLAGS = -lfuse3 -pthread
TARGET = kubsh

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = $(BUILD_DIR)/bin
SRC_FILES = $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES = $(SRC_FILES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

VERSION = 0.1
PACKAGE_NAME = kubsh
DEB_DIR = $(BUILD_DIR)/$(PACKAGE_NAME)_$(VERSION)_amd64

all: $(TARGET)

$(TARGET): $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	$(BIN_DIR)/$(TARGET)

prepare-deb: $(TARGET)
	@echo "Подготовка структуры для deb-пакета..."
	@mkdir -p $(DEB_DIR)/DEBIAN
	@mkdir -p $(DEB_DIR)/usr/bin
	@cp $(BIN_DIR)/$(TARGET) $(DEB_DIR)/usr/bin/
	@chmod 755 $(DEB_DIR)/usr/bin/$(TARGET)
	
	@echo "Создание control файла..."
	@echo "Package: $(PACKAGE_NAME)" > $(DEB_DIR)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Section: utils" >> $(DEB_DIR)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_DIR)/DEBIAN/control
	@echo "Architecture: amd64" >> $(DEB_DIR)/DEBIAN/control
	@echo "Maintainer: Dmitriy Postrichev <dima@email.com>" >> $(DEB_DIR)/DEBIAN/control
	@echo "Depends: libfuse3-4, fuse3, coreutils, util-linux, passwd, adduser" >> $(DEB_DIR)/DEBIAN/control
	@echo "Description: Custom shell with VFS for user management" >> $(DEB_DIR)/DEBIAN/control
	@echo " Implements all Kubsh requirements." >> $(DEB_DIR)/DEBIAN/control
	@chmod 644 $(DEB_DIR)/DEBIAN/control
	
	@echo "Копирование postinst скрипта..."
	@cp debian/postinst $(DEB_DIR)/DEBIAN/ 2>/dev/null || (echo '#!/bin/sh\nset -e\n\nmkdir -p /opt/users\nchmod 777 /opt/users\n\nif [ -e /dev/fuse ]; then\n    chmod 666 /dev/fuse || true\nfi\n\nmodprobe fuse || true\n\nexit 0' > $(DEB_DIR)/DEBIAN/postinst)
	@chmod 755 $(DEB_DIR)/DEBIAN/postinst

deb: prepare-deb
	@echo "Сборка deb-пакета..."
	dpkg-deb --build --root-owner-group $(DEB_DIR) .
	@mv $(PACKAGE_NAME)_$(VERSION)_amd64.deb kubsh.deb
	@echo "Пакет создан: kubsh.deb"

clean:
	rm -rf $(BUILD_DIR) 2>/dev/null || sudo rm -rf $(BUILD_DIR) 2>/dev/null || true
	rm -f kubsh.deb $(TARGET) *.o 2>/dev/null || true

.PHONY: all run deb clean prepare-deb

