#include "../src/rack/PublisherNames.hpp"
#include "../src/rack/AdapterDiagnostics.hpp"

#include <cassert>
#include <iostream>

int main() {
    rvx::rackadapter::PublisherNameReservations names;

    // Reloaded automatic identity is reserved before a fresh module chooses.
    assert(names.reserve(1, "RVX 2", true) == "RVX 2");
    assert(names.reserve(2, "RVX", true) == "RVX");

    // Rack duplication keeps automatic semantics but receives a free suffix.
    assert(names.reserve(3, "RVX", true) == "RVX 3");

    // Renaming releases the automatic claim. A later default can use it.
    names.observe(2, "Camera output");
    assert(!names.isAutomatic(2));
    assert(names.reserve(4, "RVX", true) == "RVX");

    // Explicit duplicates are preserved for visible collision diagnostics.
    assert(names.reserve(5, "Custom", false) == "Custom");
    assert(names.reserve(6, "Custom", false) == "Custom");
    assert(names.hasCollision(5));
    assert(names.hasCollision(6));

    // Deletion releases a saved automatic identity for reuse.
    names.release(1);
    assert(names.reserve(7, "RVX 2", true) == "RVX 2");

    rvx::rackadapter::NativeCableDiagnostics cables;
    assert(cables.replaceInvalidOutputs({{11, 1}}));
    assert(cables.invalidOutputCount(11) == 1);
    assert(!cables.replaceInvalidOutputs({{11, 1}}));
    assert(cables.replaceInvalidOutputs({{11, 2}}));
    assert(cables.invalidOutputCount(11) == 2);
    assert(cables.replaceInvalidOutputs({}));
    assert(cables.invalidOutputCount(11) == 0);

    std::cout << "rack adapter publisher reservations passed\n";
    return 0;
}
