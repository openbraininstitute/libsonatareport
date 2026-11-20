// A summed report across ranks, read back from its file. Rank 1, when there is one, has no node of
// the report, and population PopB is only on the last rank, so the first rank writes a sum it has
// no node of. Each rank's nodes are recorded like those of two threads, one after the other.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <hdf5.h>
#include <iostream>
#include <string>
#include <vector>

#ifdef SONATA_REPORT_HAVE_MPI
#include <mpi.h>
#endif

#include <bbp/sonata/reports.h>

static const char* report_name = "summed_report_test";
static const std::vector<double> reporting_steps = {2, 4, 6, 8};
static const size_t pop_a_elements = 3;
static const size_t pop_b_elements = 2;

static bool has_pop_a(int rank) {
    return rank != 1;
}

static bool has_pop_b(int rank, int size) {
    return rank == size - 1;
}

static double pop_a_value(int rank, size_t node, size_t element, double step) {
    return 100.0 * (rank + 1) + 10.0 * node + element + step;
}

// Summed in floats, these would lose the half
static double pop_b_value(size_t node, size_t element, double step) {
    if (element == 0) {
        return node == 0 ? 1e8 + step : -1e8 + 0.5;
    }
    return node == 0 ? 1.0 : step;
}

struct Cell {
    uint64_t gid;
    std::string population;
    std::vector<double> values;
};

template <typename T>
static std::vector<T> read(hid_t file, const std::string& name, hid_t type) {
    const hid_t dataset = H5Dopen(file, name.c_str(), H5P_DEFAULT);
    const hid_t space = H5Dget_space(dataset);
    std::vector<T> values(H5Sget_simple_extent_npoints(space));
    H5Dread(dataset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data());
    H5Sclose(space);
    H5Dclose(dataset);
    return values;
}

static bool check_population(hid_t file,
                             const std::string& population,
                             const std::vector<double>& expected) {
    const std::string group = "/report/" + population;
    const size_t elements = expected.size() / reporting_steps.size();
    std::vector<uint32_t> element_ids(elements);
    for (size_t i = 0; i < elements; ++i) {
        element_ids[i] = i;
    }
    bool ok = read<uint64_t>(file, group + "/mapping/node_ids", H5T_NATIVE_UINT64) ==
              std::vector<uint64_t>{0};
    ok = ok && read<uint64_t>(file, group + "/mapping/index_pointers", H5T_NATIVE_UINT64) ==
                   std::vector<uint64_t>{0, elements};
    ok = ok &&
         read<uint32_t>(file, group + "/mapping/element_ids", H5T_NATIVE_UINT32) == element_ids;
    const auto data = read<float>(file, group + "/data", H5T_NATIVE_FLOAT);
    ok = ok && data.size() == expected.size();
    for (size_t i = 0; ok && i < data.size(); ++i) {
        ok = std::abs(data[i] - expected[i]) < 1e-3;
    }
    if (!ok) {
        std::cerr << "Population " << population << " is not the sum expected\n";
    }
    return ok;
}

int main() {
    int rank = 0;
    int size = 1;
#ifdef SONATA_REPORT_HAVE_MPI
    MPI_Init(nullptr, nullptr);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
#endif

    const double dt = 0.1;
    sonata_set_atomic_step(dt);
    std::vector<Cell> cells;
    if (has_pop_a(rank)) {
        for (uint64_t node = 0; node < 2; ++node) {
            cells.push_back({1001 + 2 * rank + node, "PopA", std::vector<double>(pop_a_elements)});
        }
    }
    if (has_pop_b(rank, size)) {
        for (uint64_t node = 0; node < 2; ++node) {
            cells.push_back({2001 + node, "PopB", std::vector<double>(pop_b_elements)});
        }
    }
    if (!cells.empty()) {
        sonata_create_report(report_name, 0.2, 1.0, 2 * dt, "mV", "lfp_sum");
    }
    for (auto& cell : cells) {
        const uint64_t offset = cell.population == "PopA" ? 1000 : 2000;
        sonata_add_node(report_name, cell.population.c_str(), offset, cell.gid);
        for (size_t i = 0; i < cell.values.size(); ++i) {
            sonata_add_element(report_name, cell.population.c_str(), cell.gid, i, &cell.values[i]);
        }
    }
    sonata_set_min_steps_to_record(2);
    sonata_setup_reports();

    // 10 steps of the simulation, in spike exchange intervals of 3
    for (int first = 0; first < 10; first += 3) {
        for (auto& cell : cells) {
            const int gid = cell.gid;
            for (int step = first; step < std::min(first + 3, 10); ++step) {
                for (size_t i = 0; i < cell.values.size(); ++i) {
                    cell.values[i] = cell.population == "PopA"
                                         ? pop_a_value(rank, (gid - 1001 - 2 * rank), i, step)
                                         : pop_b_value(gid - 2001, i, step);
                }
                sonata_record_node_data(step, 1, &gid, report_name);
            }
        }
        sonata_check_and_flush((first + 3) * dt);
    }
    sonata_flush(1.0);
    sonata_clear();

#ifdef SONATA_REPORT_HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    bool ok = true;
    if (rank == 0) {
        std::vector<double> pop_a;
        std::vector<double> pop_b;
        for (double step : reporting_steps) {
            for (size_t element = 0; element < pop_a_elements; ++element) {
                double total = 0.0;
                for (int r = 0; r < size; ++r) {
                    for (size_t node = 0; has_pop_a(r) && node < 2; ++node) {
                        total += pop_a_value(r, node, element, step);
                    }
                }
                pop_a.push_back(total);
            }
            for (size_t element = 0; element < pop_b_elements; ++element) {
                pop_b.push_back(pop_b_value(0, element, step) + pop_b_value(1, element, step));
            }
        }
        const std::string file_name = std::string(report_name) + ".h5";
        const hid_t file = H5Fopen(file_name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
        ok = check_population(file, "PopA", pop_a) && check_population(file, "PopB", pop_b);
        H5Fclose(file);
    }
#ifdef SONATA_REPORT_HAVE_MPI
    MPI_Bcast(&ok, 1, MPI_CXX_BOOL, 0, MPI_COMM_WORLD);
    MPI_Finalize();
#endif
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
