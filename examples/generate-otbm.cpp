#include "otbm_parser.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>

// Helper to write bytes to buffer
class OTBMWriter {
    std::vector<uint8_t> buffer;
    
    void write_byte(uint8_t b) {
        // OTBM escapes all three control bytes (0xFD escape, 0xFE node start,
        // 0xFF node end); the reader un-escapes any 0xFD-prefixed byte.
        if (b == 0xFD || b == 0xFE || b == 0xFF) buffer.push_back(0xFD);
        buffer.push_back(b);
    }
    
    // Node start/end markers (0xFE/0xFF) are STRUCTURAL and written raw.
    // Only data bytes are escaped (see write_byte).
    void write_node_start() { buffer.push_back(0xFE); }
    void write_node_end() { buffer.push_back(0xFF); }
    
    template<typename T>
    void write(T value) {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            write_byte(ptr[i]);
        }
    }
    
    void write_string(const std::string& str) {
        write<uint16_t>(static_cast<uint16_t>(str.size()));
        for (char c : str) write_byte(static_cast<uint8_t>(c));
    }
    
    void write_node(uint8_t type, const std::function<void()>& write_data) {
        write_node_start();
        write_byte(type);
        write_data();
        write_node_end();
    }

public:
    void generate() {
        // OTBM version major
        write<uint32_t>(0);
        
        // Root node
        write_node(0x00, [&]() {
            // Map version, dimensions, items version
            write<uint32_t>(1);      // Map version major
            write<uint16_t>(100);    // Width
            write<uint16_t>(100);    // Height
            write<uint32_t>(3);      // Items version major
            write<uint32_t>(0);      // Items version minor
            
            // MapData node
            write_node(0x02, [&]() {
                // Description
                write_byte(0x01); // DataType::Description
                write_string("Integration Test Map - Generated Fixture");
                
                // Spawn file
                write_byte(0x0B); // DataType::ExtSpawnFile
                write_string("spawns.xml");
                
                // House file
                write_byte(0x0D); // DataType::ExtHouseFile
                write_string("houses.xml");
                
                // TileArea node (1 tile at 50,50,7)
                write_node(0x04, [&]() {
                    write<uint16_t>(50); // X
                    write<uint16_t>(50); // Y
                    write<uint8_t>(7);   // Z
                    
                    // Tile node
                    write_node(0x05, [&]() {
                        write<uint8_t>(0); // dx
                        write<uint8_t>(0); // dy
                        
                        // Item on tile (type 100)
                        write_node(0x06, [&]() {
                            write<uint16_t>(100); // Item type
                            write_byte(0x0F);     // DataType::Count
                            write<uint8_t>(5);    // Count = 5
                        });
                    });
                });
                
                // Towns node
                write_node(0x0C, [&]() {
                    // Town node
                    write_node(0x0D, [&]() {
                        write<uint32_t>(1);       // Town ID
                        write_string("Test Town"); // Town name
                        write<uint16_t>(50);      // X
                        write<uint16_t>(50);      // Y
                        write<uint8_t>(7);        // Z
                    });
                });
                
                // Waypoints node
                write_node(0x0F, [&]() {
                    // Waypoint node
                    write_node(0x10, [&]() {
                        write_string("Test Waypoint"); // Waypoint name
                        write<uint16_t>(50);           // X
                        write<uint16_t>(50);           // Y
                        write<uint8_t>(7);             // Z
                    });
                });
            });
        });
    }
    
    bool save(const std::string& path) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream file(path, std::ios::binary);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        return true;
    }
};

int generate_main() {
    std::cout << "Generating OTBM fixture...\n";
    
    OTBMWriter writer;
    writer.generate();
    
    std::string output_path = "tests/fixture/world.otbm";
    if (writer.save(output_path)) {
        std::cout << "✓ Fixture saved to: " << output_path << "\n";
        return 0;
    } else {
        std::cerr << "✗ Failed to save fixture\n";
        return 1;
    }
}
