#include "trunkmonkey/SipTrace.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace trunkmonkey;

namespace {
void check(bool condition, const char* expression)
{
    if (!condition) throw std::runtime_error(std::string("CHECK failed: ") + expression);
}
}

int main()
{
    try {
        SipTraceClassifierState state;

        SipTraceEntry initial;
        initial.method = "invite";
        initial.cseq = 41;
        classifySipTraceEntry(initial, state);
        check(initial.label == "INVITE", "initial INVITE classification");

        SipTraceEntry trying;
        trying.method = "INVITE";
        trying.cseq = 41;
        trying.statusCode = 100;
        trying.reason = "Trying";
        classifySipTraceEntry(trying, state);
        check(trying.label == "100 Trying (INVITE)", "initial INVITE response classification");

        // An auth/retry of the initial INVITE can use another CSeq but is still
        // outside the dialog (no To-tag), so it must NOT be called a re-INVITE.
        SipTraceEntry retry;
        retry.method = "INVITE";
        retry.cseq = 42;
        retry.inDialogRequest = false;
        classifySipTraceEntry(retry, state);
        check(retry.label == "INVITE", "auth retry is not classified as RE-INVITE");

        SipTraceEntry retryOk;
        retryOk.method = "INVITE";
        retryOk.cseq = 42;
        retryOk.statusCode = 200;
        retryOk.reason = "OK";
        classifySipTraceEntry(retryOk, state);
        check(retryOk.label == "200 OK (INVITE)", "auth retry response classification");

        // A true in-dialog INVITE has a To-tag and its response is associated by CSeq.
        SipTraceEntry reinvite;
        reinvite.method = "INVITE";
        reinvite.cseq = 43;
        reinvite.inDialogRequest = true;
        classifySipTraceEntry(reinvite, state);
        check(reinvite.label == "RE-INVITE", "in-dialog INVITE classification");

        SipTraceEntry reinviteOk;
        reinviteOk.method = "INVITE";
        reinviteOk.cseq = 43;
        reinviteOk.statusCode = 200;
        reinviteOk.reason = "OK";
        classifySipTraceEntry(reinviteOk, state);
        check(reinviteOk.label == "200 OK (RE-INVITE)", "RE-INVITE response classification");

        SipTraceEntry bye;
        bye.method = "bye";
        bye.cseq = 44;
        bye.inDialogRequest = true;
        classifySipTraceEntry(bye, state);
        check(bye.label == "BYE", "BYE classification");

        std::cout << "sip trace classifier tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "sip trace classifier test failed: " << e.what() << '\n';
        return 1;
    }
}
