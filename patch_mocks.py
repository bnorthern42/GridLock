import glob
import re

files = glob.glob('tests/test_dap_*.cpp')
for f in files:
    with open(f, 'r') as file:
        content = file.read()
    
    # Check if we already patched
    if 'MockDapCoordinator' in content and 'm_state = SessionState::Running;' not in content:
        # Find public: and insert the constructor
        content = re.sub(
            r'public:',
            'public:\n    MockDapCoordinator() { m_state = SessionState::Running; }',
            content,
            count=1
        )
        content = re.sub(
            r'MockDapCoordinatorLifecycle\(\) \{',
            'MockDapCoordinatorLifecycle() { m_state = SessionState::Running;',
            content
        )
        content = re.sub(
            r'MockDapCoordinatorMemory\(\) \{',
            'MockDapCoordinatorMemory() { m_state = SessionState::Running;',
            content
        )
        content = re.sub(
            r'MockDapCoordinatorEval\(\) \{',
            'MockDapCoordinatorEval() { m_state = SessionState::Running;',
            content
        )
        content = re.sub(
            r'MockDapCoordinatorVars\(\) \{',
            'MockDapCoordinatorVars() { m_state = SessionState::Running;',
            content
        )
        
        with open(f, 'w') as file:
            file.write(content)
        print(f"Patched {f}")
    
