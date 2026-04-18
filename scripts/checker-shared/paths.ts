import fs from 'fs';
import path from 'path';

function findProjectRoot(startDir: string): string {
  let current = path.resolve(startDir);

  while (true) {
    if (fs.existsSync(path.join(current, '.git')) || fs.existsSync(path.join(current, 'CMakeLists.txt'))) {
      return current;
    }

    const parent = path.dirname(current);
    if (parent === current) {
      return path.resolve(startDir);
    }
    current = parent;
  }
}

export const PROJECT_ROOT = findProjectRoot(process.cwd());
export const ROOT_ENV_FILE = path.join(PROJECT_ROOT, '.env');
export const ROOT_DATA_DIR = path.join(PROJECT_ROOT, 'data');
export const ROOT_RESULT_DIR = path.join(PROJECT_ROOT, 'result');

export function loadEnvFile(filePath: string = ROOT_ENV_FILE): Record<string, string> {
  if (!fs.existsSync(filePath)) {
    return {};
  }

  return Object.fromEntries(
    fs
      .readFileSync(filePath, 'utf-8')
      .split('\n')
      .map((line) => line.trim())
      .filter((line) => line && !line.startsWith('#') && line.includes('='))
      .map((line) => {
        const [key, ...rest] = line.split('=');
        return [key.trim(), rest.join('=').replace(/^['\"]|['\"]$/g, '').trim()];
      })
  );
}

export function ensureRootDirectories(): void {
  fs.mkdirSync(ROOT_DATA_DIR, { recursive: true });
  fs.mkdirSync(ROOT_RESULT_DIR, { recursive: true });
}


export default {
  PROJECT_ROOT,
  ROOT_ENV_FILE,
  ROOT_DATA_DIR,
  ROOT_RESULT_DIR,
  loadEnvFile,
  ensureRootDirectories,
};
