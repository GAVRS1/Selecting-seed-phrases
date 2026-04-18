import chalk from 'chalk';
import ora from 'ora';

export interface ProgressInfo {
  current: number;
  total: number;
  currentItem?: string;
}

export class UIService {
  private spinner: ora.Ora | null = null;

  showHeader(): void {
    console.clear();
    console.log(chalk.yellow.bold('╔══════════════════════════════════════════════════════════════╗'));
    console.log(chalk.yellow.bold('║                     BITCOIN CHECKER                         ║'));
    console.log(chalk.yellow.bold('║            Batch + RPC checker (DB version)                 ║'));
    console.log(chalk.yellow.bold('╚══════════════════════════════════════════════════════════════╝'));
    console.log();
  }

  startProgress(message: string): void {
    this.spinner = ora({ text: message, spinner: 'dots12', color: 'yellow' }).start();
  }

  updateProgress(info: ProgressInfo): void {
    if (!this.spinner) return;
    const percentage = info.total === 0 ? 100 : Math.round((info.current / info.total) * 100);
    this.spinner.text = `[${percentage}%] ${info.current}/${info.total}${info.currentItem ? ` - ${info.currentItem}` : ''}`;
  }

  succeedProgress(message: string): void {
    if (this.spinner) {
      this.spinner.succeed(chalk.green(message));
      this.spinner = null;
    }
  }

  showInfo(message: string): void {
    console.log(chalk.blue(`ℹ️  ${message}`));
  }

  showWarning(message: string): void {
    console.log(chalk.yellow(`⚠️  ${message}`));
  }

  showSuccess(message: string): void {
    console.log(chalk.green(`✅ ${message}`));
  }
}
