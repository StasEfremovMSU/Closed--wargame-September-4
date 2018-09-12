using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class gen_stat {
	/// Это reg_stat сделанные из списка tableregm, но не разделенный по частям
	public List<reg_stat> tempstats;
	/// <summary>
	/// 1 - три вдоль стоящих части
	/// 2 - три вдоль стоящих части и резерв
	/// </summary>
	public int position_type;
	public List<part_stat> parts;
	public int command;

	/// <summary>
	/// Число частей в полку	/// </summary>
	public int part_num;

	public void ram()
	{
		parts = new List<part_stat> ();
		tempstats = new List<reg_stat> ();

		for (int t = 0; t < part_num; t++)
		{
			part_stat x = new part_stat ();
			x.ram ();
			parts.Add(x);
		}
	}

	public void makeGroupPoints()
	{
		float temp = map.xsize / 3 * AI.Scale;
		float tempy = map.ysize / 8 * 2 * AI.Scale;
		float xcenter = 0 -  map.xsize / 2 * AI.Scale;
		float ycenter = 0 -  map.ysize / 2 * AI.Scale;
	switch (position_type)
	{
	case (1):
		// Случай где три части в ряд
		/// Пока просто треть карты у нас занята, не касается численности
		///  По оси y все рассматриваем как 1/8
		for (int i = 0; i < parts.Count; i++)
		{
			parts[i].points[0].x = temp + temp / 3 * i + xcenter;
				parts[i].points[0].y = tempy + ycenter;
				parts[i].points[1].x = temp + temp / 3 + temp / 3 * i + xcenter;
				parts[i].points[1].y = tempy + ycenter;
				parts[i].points[2].x = temp + temp / 3 + temp / 3 * i + xcenter;
				parts[i].points[2].y = tempy/2 + ycenter;
				parts[i].points[3].x = temp + temp / 3 * i + xcenter;
				parts[i].points[3].y = tempy/2 + ycenter;
		}

		break;

		case (2):
		// Случай где три части в ряд, а сзади резерв
		/// Пока просто треть карты у нас занята, не касается численности
		///  По оси y все рассматриваем как 1/8
			for (int i = 0; i < (parts.Count - 1); i++) {
				parts [i].points [0].x = temp + temp / 3 * i  + xcenter;
				parts [i].points [0].y = tempy + ycenter;
				parts [i].points [1].x = temp + temp / 3 + temp / 3 * i + xcenter;
				parts [i].points [1].y = tempy + ycenter;
				parts [i].points [2].x = temp + temp / 3 + temp / 3 * i + xcenter;
				parts [i].points [2].y = tempy / 2 + ycenter;
				parts [i].points [3].x = temp + temp / 3 * i + xcenter;
				parts [i].points [3].y = tempy / 2 + ycenter;
			}
			parts[3].points[0].x = temp + temp / 3 + xcenter;
			parts[3].points[0].y = tempy/2 + ycenter;
			parts[3].points[1].x = temp + temp / 3 + temp / 3 + xcenter;
			parts[3].points[1].y = tempy/2 + ycenter;
			parts[3].points[2].x = temp + temp / 3 + temp / 3 + xcenter;
			parts[3].points[2].y = 0 + ycenter;
			parts[3].points[3].x = temp + temp / 3 + xcenter;
			parts[3].points[3].y = 0 + ycenter;
		break;
	}
	/// В таком виде справедливо только для первой команды
	if (command != 0)
	{
		for (int t = 0; t < parts.Count; t++)
		{
				parts[t].points[0].y = parts[t].points[0].y * (-1);
				parts[t].points[1].y = parts[t].points[1].y * (-1);// + map.ysize * AI.Scale;
				parts[t].points[2].y = parts[t].points[2].y * (-1);
				parts[t].points[3].y = parts[t].points[3].y * (-1);
		}
	}
			
}
		
	public void MakeParts ()
	{
		/// Раскладваем полки по частям
		/// сколько полков всего
		int num_regs = tempstats.Count;
		int h = 0;
		// Риторический вопрос - как разделить на н частей
		// Делим на количество частей
		int r = (int) num_regs/part_num;
		// Добавим в каждую часть по r, а в центральную - остаток от деления
		int count = 0;
		for (int i = 0; i < part_num; i++) 
		{
			for ( int j = 0 ; j < r; j++) 
			{
				parts [i].regiments.Add (tempstats[count]);
				parts [i].regiments [parts [i].regiments.Count - 1].part_number = r;
				count++;
			}
//			Debug.Log (parts[i].regiments.Count);
		}

		for (int i = r * part_num; i < num_regs; i++) {
			parts [1].regiments.Add (tempstats[count]);
			parts [1].regiments [parts [1].regiments.Count - 1].part_number = parts[1].regiments.Count;
			count++;
		}
	}
}