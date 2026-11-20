#pragma once
#include "element_report.h"

namespace bbp {
namespace sonata {

/**
 * Writes the sum of each population's nodes: one trace per element id, in one node of the file,
 * node 0, which stands for all of the population's nodes.
 *
 * Each rank adds its nodes to rows of doubles, one row per reporting step. check_and_flush() sums
 * the rows across the report's ranks, and the first of them records the totals in the node it
 * writes. One collective per call, at a point where the ranks are already synchronised.
 */
class SummedReport: public ElementReport
{
  public:
    using ElementReport::ElementReport;

    void record_data(double step, const std::vector<uint64_t>& node_ids) override;
    void record_data(double step) override;
    void check_and_flush(double timestep) override;
    void flush(double time) override;

  protected:
    std::shared_ptr<nodes_t> nodes_to_write(const std::string& population_name,
                                            std::shared_ptr<nodes_t> nodes) override;

  private:
    struct Sum {
        std::shared_ptr<nodes_t> nodes;  // the nodes recorded on this rank
        uint64_t written_node;
        size_t num_elements;
        std::vector<double> steps;   // reporting steps whose rows are not recorded yet
        std::vector<double> rows;    // num_elements values per step
        std::vector<double> totals;  // what the written node reads, on the first rank
    };

    void record_sums();

    // In the order of sonata_populations_
    std::vector<Sum> sums_;
};

}  // namespace sonata
}  // namespace bbp
