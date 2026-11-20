#include <algorithm>
#include <stdexcept>

#include "implementation_interface.hpp"
#include "summed_report.h"

namespace bbp {
namespace sonata {

std::shared_ptr<nodes_t> SummedReport::nodes_to_write(const std::string& population_name,
                                                      std::shared_ptr<nodes_t> nodes) {
    size_t local_elements = 0;
    for (const auto& kv : *nodes) {
        local_elements = std::max(local_elements, kv.second->get_num_elements());
    }
    const size_t num_elements =
        Implementation::get_max_elements(report_name(), static_cast<uint32_t>(local_elements));
    for (const auto& kv : *nodes) {
        const auto& ids = kv.second->get_element_ids();
        const bool in_range =
            std::all_of(ids.begin(), ids.end(), [&](uint32_t id) { return id < num_elements; });
        if (ids.size() != num_elements || !in_range) {
            throw std::runtime_error("Summed report '" + report_name() + "': node " +
                                     std::to_string(kv.first) + " of population '" +
                                     population_name + "' must have element ids 0 to " +
                                     std::to_string(num_elements - 1) + ", as the others have");
        }
    }

    Sum sum{nodes,
            SonataData::first_gid(population_offsets_[population_name]),
            num_elements,
            {},
            {},
            {}};
    auto written = std::make_shared<nodes_t>();
    if (Implementation::get_rank(report_name()) == 0) {
        sum.totals.resize(num_elements);
        auto node = std::make_shared<Node>(sum.written_node);
        for (size_t i = 0; i < num_elements; ++i) {
            node->add_element(&sum.totals[i], static_cast<uint32_t>(i));
        }
        written->emplace(sum.written_node, node);
    }
    sums_.push_back(std::move(sum));
    return written;
}

void SummedReport::record_data(double step, const std::vector<uint64_t>& node_ids) {
    for (size_t i = 0; i < sums_.size(); ++i) {
        if (!sonata_populations_[i]->is_due_to_report(step)) {
            continue;
        }
        Sum& sum = sums_[i];
        // The threads of a rank each go through the steps in order, but not in step with each other
        const auto found = std::find(sum.steps.begin(), sum.steps.end(), step);
        const size_t row = found - sum.steps.begin();
        if (found == sum.steps.end()) {
            sum.steps.push_back(step);
            sum.rows.resize(sum.steps.size() * sum.num_elements, 0.0);
        }
        for (const uint64_t node_id : node_ids) {
            const auto node = sum.nodes->find(node_id);
            if (node != sum.nodes->end()) {
                node->second->add_data(sum.rows.begin() + row * sum.num_elements);
            }
        }
    }
}

void SummedReport::record_data(double /*step*/) {
    throw std::runtime_error("Summed report '" + report_name() + "' is recorded by node");
}

void SummedReport::check_and_flush(double timestep) {
    record_sums();
    Report::check_and_flush(timestep);
}

void SummedReport::flush(double time) {
    record_sums();
    Report::flush(time);
}

void SummedReport::record_sums() {
    for (size_t i = 0; i < sums_.size(); ++i) {
        Sum& sum = sums_[i];
        if (sum.steps.empty()) {
            continue;
        }
        Implementation::sum_on_first_rank(report_name(), sum.rows);
        for (size_t row = 0; row < sum.steps.size(); ++row) {
            if (!sum.totals.empty()) {
                std::copy_n(sum.rows.begin() + row * sum.num_elements,
                            sum.num_elements,
                            sum.totals.begin());
            }
            sonata_populations_[i]->record_data(sum.steps[row], {sum.written_node});
        }
        sum.steps.clear();
        sum.rows.clear();
    }
}

}  // namespace sonata
}  // namespace bbp
